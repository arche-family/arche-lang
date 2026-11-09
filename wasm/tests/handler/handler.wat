;; An HTTP handler for `arche-wasm serve` (docs/sprints.md S33), by hand in
;; the text format. The server writes a request into memory it asks of
;; alloc, "METHOD TARGET\n" and the body, and calls handle, which gives back
;; where its response is and how long it is (ptr << 32 | len): "STATUS
;; CONTENT-TYPE\n" and the body. It can reach the host only through what it
;; imports: a log line, and the time.
;;
;;   GET  /hello   a greeting
;;   GET  /count   how many requests this instance has answered
;;   GET  /time    {"now_ms":n}
;;   POST /echo    the body back
;;   GET  /spin    never returns: the server's deadline stops it
;;   GET  /grow    grows memory until the server's limit stops it
;;   else          404
(module
  (import "arche" "log" (func $log (param i32 i32)))
  (import "arche" "now_ms" (func $now_ms (result i64)))
  (memory (export "memory") 2)
  (global $heap (mut i32) (i32.const 4096))
  (global $count (mut i64) (i64.const 0))

  (data (i32.const 64) "GET /hello")
  (data (i32.const 80) "GET /count")
  (data (i32.const 96) "GET /time")
  (data (i32.const 112) "POST /echo")
  (data (i32.const 128) "GET /spin")
  (data (i32.const 144) "GET /grow")
  (data (i32.const 160) "200 text/plain\nhello from a wasm handler\n")
  (data (i32.const 208) "404 text/plain\nno such handler\n")
  (data (i32.const 240) "200 application/json\n{\"now_ms\":")
  (data (i32.const 280) "200 text/plain\nrequests: ")
  (data (i32.const 312) "200 application/octet-stream\n")
  (data (i32.const 344) "handler: a request")

  ;; Memory for the host to write a request into: from a heap that starts
  ;; again with each request, grown as need be.
  (func (export "alloc") (param $n i32) (result i32)
    (local $at i32)
    (global.set $heap (i32.const 4096))
    (local.set $at (global.get $heap))
    (call $room (i32.add (local.get $at) (local.get $n)))
    (global.set $heap (i32.add (local.get $at) (local.get $n)))
    (local.get $at))

  ;; Enough pages for end bytes.
  (func $room (param $end i32)
    (block $done
      (loop $more
        (br_if $done (i32.le_u (local.get $end) (i32.mul (memory.size) (i32.const 65536))))
        (drop (memory.grow (i32.const 1)))
        (br $more))))

  ;; Bytes from the heap, for a response.
  (func $take (param $n i32) (result i32)
    (local $at i32)
    (local.set $at (global.get $heap))
    (call $room (i32.add (local.get $at) (local.get $n)))
    (global.set $heap (i32.add (local.get $at) (local.get $n)))
    (local.get $at))

  ;; Whether the request starts with the n bytes at lit, then " ", "\n" or "?".
  (func $is (param $req i32) (param $len i32) (param $lit i32) (param $n i32) (result i32)
    (local $i i32) (local $c i32)
    (if (i32.lt_u (local.get $len) (local.get $n)) (then (return (i32.const 0))))
    (block $differ
      (loop $next
        (if (i32.eq (local.get $i) (local.get $n))
          (then
            (if (i32.eq (local.get $len) (local.get $n)) (then (return (i32.const 1))))
            (local.set $c (i32.load8_u (i32.add (local.get $req) (local.get $n))))
            (return (i32.or (i32.or (i32.eq (local.get $c) (i32.const 32)) (i32.eq (local.get $c) (i32.const 10)))
                            (i32.eq (local.get $c) (i32.const 63))))))
        (br_if $differ (i32.ne (i32.load8_u (i32.add (local.get $req) (local.get $i)))
                               (i32.load8_u (i32.add (local.get $lit) (local.get $i)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next)))
    (i32.const 0))

  ;; A response of the n bytes at from, copied, and then the decimal of v
  ;; and the n2 bytes at tail when v is not -1.
  (func $respond (param $from i32) (param $n i32) (param $v i64) (param $tail i32) (param $n2 i32) (result i64)
    (local $out i32) (local $len i32) (local $digits i32) (local $x i64) (local $k i32)
    (local.set $out (call $take (i32.add (local.get $n) (i32.add (local.get $n2) (i32.const 24)))))
    (memory.copy (local.get $out) (local.get $from) (local.get $n))
    (local.set $len (local.get $n))
    (if (i64.ge_s (local.get $v) (i64.const 0))
      (then
        ;; The digits, backwards into scratch at 512, then in order.
        (local.set $x (local.get $v))
        (loop $digit
          (i32.store8 (i32.add (i32.const 512) (local.get $digits))
                      (i32.add (i32.const 48) (i32.wrap_i64 (i64.rem_u (local.get $x) (i64.const 10)))))
          (local.set $digits (i32.add (local.get $digits) (i32.const 1)))
          (local.set $x (i64.div_u (local.get $x) (i64.const 10)))
          (br_if $digit (i64.ne (local.get $x) (i64.const 0))))
        (local.set $k (local.get $digits))
        (loop $copy
          (local.set $k (i32.sub (local.get $k) (i32.const 1)))
          (i32.store8 (i32.add (local.get $out) (local.get $len)) (i32.load8_u (i32.add (i32.const 512) (local.get $k))))
          (local.set $len (i32.add (local.get $len) (i32.const 1)))
          (br_if $copy (i32.ne (local.get $k) (i32.const 0))))))
    (memory.copy (i32.add (local.get $out) (local.get $len)) (local.get $tail) (local.get $n2))
    (local.set $len (i32.add (local.get $len) (local.get $n2)))
    (i64.or (i64.shl (i64.extend_i32_u (local.get $out)) (i64.const 32)) (i64.extend_i32_u (local.get $len))))

  (data (i32.const 400) "}\n")
  (data (i32.const 408) "\n")

  (func (export "handle") (param $req i32) (param $len i32) (result i64)
    (local $body i32) (local $out i32) (local $n i32)
    (global.set $count (i64.add (global.get $count) (i64.const 1)))
    (call $log (i32.const 344) (i32.const 18))
    (if (call $is (local.get $req) (local.get $len) (i32.const 64) (i32.const 10))
      (then (return (call $respond (i32.const 160) (i32.const 41) (i64.const -1) (i32.const 0) (i32.const 0)))))
    (if (call $is (local.get $req) (local.get $len) (i32.const 80) (i32.const 10))
      (then (return (call $respond (i32.const 280) (i32.const 25) (global.get $count) (i32.const 408) (i32.const 1)))))
    (if (call $is (local.get $req) (local.get $len) (i32.const 96) (i32.const 9))
      (then (return (call $respond (i32.const 240) (i32.const 31) (call $now_ms) (i32.const 400) (i32.const 2)))))
    (if (call $is (local.get $req) (local.get $len) (i32.const 112) (i32.const 10))
      (then
        ;; The body: after the first line.
        (block $found
          (loop $find
            (br_if $found (i32.ge_u (local.get $body) (local.get $len)))
            (br_if $found (i32.eq (i32.load8_u (i32.add (local.get $req) (local.get $body))) (i32.const 10)))
            (local.set $body (i32.add (local.get $body) (i32.const 1)))
            (br $find)))
        (local.set $body (i32.add (local.get $body) (i32.const 1)))
        (local.set $n (i32.sub (local.get $len) (local.get $body)))
        (return (call $respond (i32.const 312) (i32.const 29) (i64.const -1) (i32.add (local.get $req) (local.get $body)) (local.get $n)))))
    (if (call $is (local.get $req) (local.get $len) (i32.const 128) (i32.const 9))
      (then (loop $forever (br $forever))))
    (if (call $is (local.get $req) (local.get $len) (i32.const 144) (i32.const 9))
      (then (loop $more (br_if $more (i32.ne (memory.grow (i32.const 16)) (i32.const -1))))))
    (call $respond (i32.const 208) (i32.const 31) (i64.const -1) (i32.const 0) (i32.const 0)))
)
