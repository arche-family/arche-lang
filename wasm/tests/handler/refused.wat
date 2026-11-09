;; A handler that imports what the server does not give: refused at load.
(module
  (import "arche" "exit" (func $exit (param i32)))
  (memory (export "memory") 1)
  (func (export "alloc") (param i32) (result i32) (i32.const 0))
  (func (export "handle") (param i32 i32) (result i64) (call $exit (i32.const 0)) (i64.const 0)))
