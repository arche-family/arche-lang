# Arche tili — sprintlar

> 2026-11-05 holati. Asos: [plan.md](plan.md) va [spec/arche-0.md](../spec/arche-0.md). Bootstrap "Real dunyoda ishga tushirish" variantida: stage0 C11'da, release backend LLVM `.ll` orqali. Arche OS sprintlari bu yerda yo'q.

**Joriy sprint:** S27. S0–S4, S6–S26 va S36 yopilgan: compiler o'zini build qiladi (stage2 == stage3), stage0 `stage0-frozen` tag'i bilan muzlatilgan, to'liq til spec'i `spec/arche.md`'da; full-Arche paketlarida generics (monomorphization), comptime interpreter, interface'lar (`impl`, `where`, `&dyn`), `for`, closure'lar (`&dyn fn(P) -> R`), `region` (arena, E0901), `unsafe` va raw pointer'lar, `if`/`match` ifoda sifatida, labeled loop'lar, `f32`/`f64`, `requires`/`ensures`, `nopanic` va error return trace ishlaydi. Manifest (`package.arche`) o'qiladi: `needs` (E0801), `strict_unsafe` (E0920), `panic_handler`, xato manifest E0802/E0803. Kutubxona `lib/`'da: `core` (OS va allocator'siz, compiler buni majburlaydi) va `alloc` Arche'da yozilgan; full-Arche paketlari shu `alloc`'ni ishlatadi, compiler ham `--lib-alloc` bilan shu kutubxona ustida bootstrap bo'ladi (stage2 == stage3). `freestanding` target'i bor: `asm`, `naked`, volatile, atomic'lar, `packed`/`align`, `section`/`export`, `extern "c"` va panic handler bilan `freestanding/`'dagi dasturlar QEMU'da x86-64 va RISC-V 64'da ishga tushib serial'ga yozadi (CI'da) — arche-os ishi boshlanishi mumkin. `std` Linux uchun Arche'da (`lib/std.arche`): Arche-0'ning `System`/`Fs`/`Output`/`Args`'i va `Clock`, `Env`, `Proc`, `Net`, `Rand` capability'lari syscall'lar orqali, `std.json`, `std.path`, `core.rand`; compiler `--lib-std` bilan shu kutubxona ustida ham bootstrap bo'ladi (stage2 == stage3); `arche test` testlarni alohida process'larda, seed bilan, filter va property-based tekshiruv bilan ishga tushiradi. Concurrency: `std.task` — structured `scope`, `spawn`, `Channel(T)`, `Shared(T)`, seed'li scheduler (bir xil seed — bir xil tartib), task capture qoidalari (E0902, E0903, E0603), deadlock aniqlanadi. Windows host'i ham bor (`arche --target windows-x86_64`): LLVM orqali PE, link `lld-link` bilan, libc'siz — entry point runtime'ning o'zida, `kernel32`/`ws2_32`/`advapi32` import kutubxonalari bilan, `-Brepro` bilan (link vaqti faylga yozilmaydi, shuning uchun bir xil kirish bir xil bayt beradi); target tanlovi bitta joyda — `compiler/target.sh`. Runtime ikki yarimga bo'lindi: portativ qism `rt.c`'da, OS'ga xosi `runtime/linux.inc` va `runtime/windows.inc`'da (xotira, fayllar, chiqish, fiber switch — Windows'da TEB'dagi stack chegaralari va o'z `__chkstk`'i bilan, netpoller WSAPoll ustida, va har test uchun bola process: Linux'da `fork`, Windows'da dastur o'zini `--a0-test <raqam> <seed>` bilan qaytadan ishga tushiradi, chiqishi ikki faylga yoziladi). `std` har host uchun alohida fayl: `lib/std.arche` (Linux, `std.linux` ustida) va `lib/std-windows.arche` (`std.win32` ustida — yo'llar UTF-16, `Fs` (`FindFirstFileW` bilan `list`), `Clock` (QueryPerformanceCounter va FILETIME), `Env` (`GetEnvironmentVariableW`), `Proc` (`CreateProcessW` + job object: bola bilan birga u boshlagan hammasi tugaydi), `Net` (Winsock, bloklamaydigan socket'lar), `Rand` (RtlGenRandom), `Input`); modul nomi ikkisida ham `std`, uni compiler target bo'yicha tanlaydi (`program.std_file`), `std.SYSTEM` esa dasturga qaysi sistemada ekanini aytadi. `Fs.temp_dir` qo'shildi (ikkisida ham), conformance runner `// system: linux|windows` kalitini tushunadi va compiler buyrug'i argumentli bo'lishi mumkin (`--compiler "sh ./arche-build"`: Windows skriptni o'zi ishga tushirmaydi); `.gitattributes` barcha matn fayllarini working tree'da ham LF'da saqlaydi, chunki Arche qatori LF bilan tugaydi va bootstrap baytlarni solishtiradi. Windows'da `make -C compiler check` to'liq o'tadi (conformance, `arche test`, roundtrip, golden diagnostika, `arche explain`, doctest), stage0 yo'li ham o'tadi (`make -C conformance check`: stage0 runtime'i Windows'da chiqishni bayt sifatida yozadi va katalogni fayldan ajratadi) va bootstrap'da stage2 == stage3 — debug, `--lib-std` va release uchun; `arche-driver` ham ishlaydi (`driver/check.sh`: cache'dan build toza build bilan bit-identical) va `driver/pkg-check.sh` ham o'tadi — ya'ni o'zimizning TLS va HTTP Windows socket'lari ustida ham ishlaydi; CI'da `windows-latest` job'i shuni takrorlaydi. Kripto `lib/crypto/`'da: SHA-256/384/512, HMAC, HKDF, ChaCha20-Poly1305, AES-128/256-GCM, X25519, Ed25519, ECDSA P-256 (RFC 6979 nonce) va ChaCha20 CSPRNG, standart vector'lar bilan; `ctcheck/` dudect usulida (Welch t-testi, ataylab sizdiradigan nazorat funksiyasi bilan) constant-time'ni CI'da har commit'da tekshiradi. TLS 1.3 `lib/tls`'da: I/O'siz `Engine` (client va server) va socket ustidagi `Conn`, uchta cipher suite, X25519 va secp256r1, ECDSA P-256 va Ed25519 imzolari, ALPN, KeyUpdate; X.509 parse, zanjir, muddat va host nomi tekshiruvi, PEM, PKCS #8/SEC 1 kalitlar va sertifikat yaratish (`x509.create`); `interop/tls.sh` OpenSSL (s_server, s_client) va curl bilan ikki tomonga interop'ni, `fuzz/tls.sh` handshake va sertifikatlar fuzzing'ini CI'da ishlatadi. HTTP `lib/http`'da: HTTP/1.1 (keep-alive, chunked ikki tomonga, 100-continue, request smuggling'ga qarshi framing qoidalari), HTTP/2 (HPACK Huffman va dinamik jadval bilan, stream multiplexing, flow control), `http.server` — har ulanish alohida task va region'da, TLS ustida ALPN bilan h2/http1.1, ochiq matnda h2 prior knowledge; kichik HTTP/1.1 client (`fetch`). Runtime'da netpoller: socket'lar bloklamaydi, kutayotgan task boshqalarga yo'l beradi (`poll`); tugagan fiber stack'lari qayta ishlatiladi; `arche test` testlarni parallel (`--jobs`) ishlatadi. `interop/http.sh` curl (HTTP/1.1, h2c, TLS ustida h2, multiplexing, katta yuklash/yuklab olish) va headless Chrome bilan, `fuzz/http_fuzz.arche` h1 parser, HPACK va h2 frame'larini CI'da tekshiradi. Toolchain buyruqlari: `arche fix` (checker tuzatishlarini qo'llaydi, apply-fixes.py o'rnida), `arche explain` (har xato kodining ma'nosi va misoli; misollar conformance testlaridan generatsiya qilinadi va har biri o'z kodini berishi tekshiriladi), `arche fmt` (sozlamasiz, lossless CST ustida; compiler va lib manbalarida o'zgarishsiz), `arche query def|refs|type|callers`, `arche doc` (Markdown va doc-comment misollari test sifatida, `compiler/doctest.sh`); conformance runner endi Arche'da (`conformance/runner/runner.arche`, `std.Command`/`Proc.spawn` ustida), C11 runner olib tashlandi. Edition'lar: 2026 va 2027 (2027'da `ok`/`err` nom, natija pattern'lari `.ok(…)`/`.err(…)`, eskisi E0212 fix bilan), `arche fix --edition` paketni ko'chiradi va qayta tekshiradi. Build: `build.arche` dasturlarni sanaydi, `driver/` (to'liq Arche'da, `arche-driver build|run|clean`) compiler'ni va clang'ni boshqaradi — compiler `arche units` bilan har modulni alohida LLVM birligi qilib beradi, har birining obyekti SHA-256 bo'yicha cache'lanadi; bitta modul o'zgarsa faqat u qayta kompilyatsiya bo'ladi va cache'dan build toza build bilan bit-identical (`driver/check.sh`). Symbol nomlari endi faqat o'z modulining matniga bog'liq (closure va impl — node raqami, instance — argumentlar hash'i). Runtime'ga libc'siz `floor/ceil/trunc/fmod/fma` (va `f` shakllari) qo'shildi — ilgari ular faqat constant-folding tufayli ishlardi. Paketlar: `arche-driver pkg add|lock|vendor|publish|keygen`; registry content-addressed (arxiv SHA-256 nomi bilan, o'zgarmas), release yozuvlari Ed25519 bilan imzolangan, `arche-registry` uni HTTPS orqali beradi (o'zimizning TLS va HTTP ustida); `package.lock` har dependency'ni hash bilan qayd etadi, `vendor/<nomi>/`dan compiler topadi, har dependency o'z manifestining `needs`/edition'iga bo'ysunadi; `driver/pkg-check.sh` imzo yoki hash buzilgan paketning, begona kalitning, ikki marta publish'ning rad etilishini va bir xil source + lockfile → bit-identical dasturni tekshiradi. Optimizatsiya: `arche --release` (`compiler/ir/opt.arche`) — kichik barg funksiyalarni inline qilish, mem2reg, SCCP, GVN (load'lar va allaqachon tekshirilgan shartlar bilan, shu tufayli sikl testi takrorlagan bounds-check'lar tushadi), LICM, DCE; konstantalardan isbotlangan contract'lar va tekshiruvlar release build'dan tushadi. Release'da barcha conformance testlari o'tadi, `bootstrap.sh` release stage2 == stage3'ni va release compiler debug compiler bilan aynan bir xil IR chiqarishini tekshiradi; baseline `bench/baseline.md`'da. S36 yopildi: `arche-lsp` (`driver/lsp.arche`, to'liq Arche) JSON-RPC orqali `didOpen`, `didChange`, `didClose`, `didSave`, `publishDiagnostics`, `definition`, `references`, `hover`, `completion`, `formatting` va `codeAction` (fix'lar) beradi; har so'rov compiler'ni alohida process'da ishga tushiradi, saqlanmagan matn `--overlay` bilan uzatiladi. Compiler `--package`, `--no-main`, `complete`, `query --json`/`hover` va test binary'si `--json`ni oldi, `std` `sys.stdin`, `Fs.current_dir`, `Fs.make_private_dir`ni oldi (Linux'da 0700, Windows'da faqat egasiga DACL). `driver/lsp-check.py` Linux va Windows'da real sessiya o'tkazadi; `compiler/main.arche` ustida `didChange`dan diagnostikagacha eng yomoni 72 ms. TextMate grammatikasi `editors/`'da, kalit so'zlari lexer bilan solishtiriladi; xabar parser'i `fuzz/lsp.sh` bilan fuzz qilinadi. Ochiq qolganlar: S5'da parser'ni 24 soat fuzz qilish (CI har commit'da 60 soniya fuzz qiladi); S11'dagi comptime interpreter hozircha faqat integer va `bool` kodni bajaradi; S12'da `where` bilan cheklangan `T` qiymatida faqat constraint method'lari qolishi hali tekshirilmaydi, generic `impl` va `core.Iter` S15'da; S13'da closure'ni `comptime F: type` bilan olish (turini nomlash) S15'da `core` bilan, `  S14'da contract shartlarining side effect'siz ekanligi hali tekshirilmaydi, release build (isbotlangan shartlarni olib tashlash) S26'da, `nopanic` isboti hozircha faqat literal'lar bilan ishlaydi; S15'da Allocator hozircha handle (dastur heap'i yoki region arena'si, runtime'da C'da): page, pool va general allocator'larni Arche turlari sifatida Allocator interface'i orqali ulash, `core.fmt`'da float, Arche-0 paketlarida standart holda kutubxona `alloc`'i (stage0 o'zinikini ishlatadi) keyinroq; S16'da `target_features` va konstantalarga `section` hali yo'q, `core.atomic.U32` kabi turlar S18'da, `naked` va `asm`'ning 32-bitlik boot kodini tekshirish faqat QEMU orqali; S17'da `Fs`'ni katalogga toraytirish (`within`) va Net'da IPv6/UDP/DNS keyinroq, Arche-0 paketlari standart holda hali compiler'ning o'z std'ini ishlatadi; S18'da task'lar bitta OS thread'da fiber sifatida ishlaydi: socket'lar S22'dan beri bloklamaydi, lekin fayl va `sleep_ns` hali hammasini to'xtatadi, ko'p protsessorda parallel worker'lar keyinroq; `core.atomic.U32` kabi turlar hali yo'q; S20'da AES dasturiy (AES-NI yo'q, sekin), X25519 TweetNaCl usulida — tezlashtirish S26'da; S21'da RSA va P-384 sertifikatlari (internetdagi ko'p serverlar), HelloRetryRequest, session resumption/0-RTT va client sertifikatlari yo'q, sertifikat bekor qilinishi (CRL/OCSP) tekshirilmaydi; S22'da HTTP/2 client, server push, HPACK encoder'ning dinamik jadvali va h2c Upgrade yo'q, javob tanasi to'liq xotirada yig'iladi (streaming yo'q), uzoq yashaydigan h2 ulanishida so'rovlar xotirasi ulanish yopilguncha qaytmaydi; S23'da `arche query` struct literal maydonlari, variant'lar va comptime parametrlarini hali bog'lamaydi, `arche fmt` qatorlarni bo'lmaydi va qo'shmaydi (faqat indentatsiya, bo'shliqlar va bo'sh qatorlar), E0801 va E0920 misollari manifest bilan S24'da qo'shildi; S24'da compiler Arche-0'da qolgani uchun `arche build`/`run` alohida `arche-driver` dasturi, front-end (tekshiruv) har build'da butun dasturni qayta tekshiradi (tez: ~60 ms), cache faqat backend (clang) ishini saqlaydi, `dependencies` o'qiladi; S25'da versiyalar aniq (range va semver yechimi yo'q, bitta paketning ikki versiyasi xato), registry host'i IPv4 yoki localhost (DNS yo'q), registry bitta kalit bilan imzolaydi (har publisher'ning o'z kaliti keyinroq); S26'da optimizer module chegarasidan o'tib inline qiladi, lekin faqat barg funksiyalarni, load'larni LICM ko'tarmaydi, `arche-driver` hali `--release` bermaydi, `ctcheck` faqat debug build'ni o'lchaydi, `nopanic` isboti optimizer natijasidan foydalanmaydi; S19'da Windows'da standart kirishni o'qish butun dasturni to'xtatadi (netpoller faqat socket'larni kuzatadi, Linux'da fayl o'qish ham shunday), `std.path` yo'l ajratuvchisi sifatida faqat `/`ni biladi (`is_absolute` disk nomini ham tanidi), kripto/TLS/HTTP interop (OpenSSL, curl, Chrome), `ctcheck`, freestanding QEMU va parser fuzzing CI'da Linux job'ida qoladi, Windows'da UTF-8 konsol uchun code page o'zgartiriladi, MSYS shell'i native programmaga `/` bilan boshlanadigan argumentni yo'l deb o'zgartiradi (shuning uchun linker flag'lari chiziqcha bilan va `pkg-check.sh`'da `MSYS2_ARG_CONV_EXCL`), runner'ning o'z selftest'i Windows'da o'tkazib yuboriladi (mock compiler ham, u yozgan "dastur" ham shell skript — Windows ularni o'zi ishga tushirmaydi; runner'ning o'zi conformance to'plami bilan sinaladi); S36'da server hujjatni butunligicha oladi (incremental sync yo'q), `rename` va workspace bo'yicha qidiruv yo'q, Karvon va VS Code uchun plagin bu repo'da emas.

## Qoidalar

- Sprint sana bilan emas, qabul mezoni bilan yopiladi. Qabul mezoni CI'da avtomatik tekshiriladi.
- Til imkoniyati bitta sprintda uch qism bilan birga keladi: spec matni, conformance testlari va implementatsiya. Har qoidaga kamida bitta ijobiy va bitta salbiy test to'g'ri keladi.
- Compiler source'i doim Arche-0'da qoladi. S9'dan boshlab har sprint to'liq bootstrap va stage2 == stage3 tekshiruvi bilan yopiladi.
- S5'dan boshlab har yangi xato kodi `arche explain` uchun misol va test bilan qo'shiladi.
- Fuzzing komponent paydo bo'lgan sprintda boshlanadi va keyin doimiy ishlaydi.
- Sprint rejadagi qarorni o'zgartirsa, `plan.md` va spec shu sprintning o'zida yangilanadi.

## Umumiy ko'rinish

| Bosqich | Sprintlar | Natija |
| --- | --- | --- |
| 0. Poydevor | S0 | test format'i, runner, CI |
| 1. stage0 | S1–S4 | Arche-0 → C tarjimon |
| 2. Self-host | S5–S9 | Arche-0'da yozilgan compiler, stage2 == stage3 |
| 3. To'liq til | S10–S14 | to'liq spec, compiler'da til imkoniyatlari |
| 4. Kutubxonalar | S15–S19 | `core`, `alloc`, `std` (Linux, Windows), `freestanding` |
| 5. Kripto, TLS, HTTP | S20–S22 | OS'ga bog'liq bo'lmagan tarmoq kutubxonalari |
| 6. Toolchain | S23–S25 | `arche` buyruqlari, build tizimi, package manager |
| 7. Backend'lar | S26–S30 | optimizatsiya, o'z backend'lari, linker, WASM |
| 8. Web va WASM | S31–S33 | WASM runtime, web server |
| 9. Yakuniy qabul | S34 | rejaning yakuniy qabul mezoni |
| 10. Telefon kutubxonalari | S35 | Arche OS telefoni uchun: shrift, PNG, D-Bus, `std.linux` qo'shimchalari |
| 11. Editor va macOS | S36–S37 | `arche-lsp` va editor integratsiyasi; macOS target |

```mermaid
flowchart LR
    f0["0. Poydevor"] --> f1["1. stage0"] --> f2["2. Self-host"] --> f3["3. To'liq til"] --> f4["4. Kutubxonalar"]
    f3 --> f7["7. Backend'lar"]
    f4 --> f5["5. Kripto, TLS, HTTP"]
    f4 --> f6["6. Toolchain"]
    f5 --> f6
    f5 --> f8["8. Web va WASM"]
    f7 --> f8
    f6 --> f9["9. Yakuniy qabul"]
    f8 --> f9
    f4 -.->|S16 dan keyin| os["arche-os"]
    f4 --> f10["10. Telefon kutubxonalari"]
    f10 -.-> os
    f6 --> f11["11. Editor va macOS"]
```

Bosqichlar raqam tartibida yoziladi. Bir nechta ishchi bo'lsa, grafikdagi parallel tarmoqlar bir vaqtda olib borilishi mumkin.

## 0. Poydevor

### S0. Infratuzilma

- `stage0/`: C11 loyiha skeleti va Makefile. gcc va clang bilan `-std=c11 -Wall -Wextra -Werror` ostida build bo'ladi.
- `conformance/` va test format'i. Har test bitta `.arche` fayl bo'ladi. Kutilgan natija fayl boshidagi izohlarda yoziladi: stdout, exit kodi yoki compile xato kodi. §12'dagi tekshiruvlarning salbiy testlari "stage0 o'tkazib yuborishi mumkin" deb belgilanadi.
- Test runner C11'da yoziladi, chunki u Arche paydo bo'lguncha ishlashi kerak. Keyin uni `arche test` almashtiradi.
- CI (GitHub Actions, Linux): har commit'da stage0 build va conformance testlari ishlaydi.
- Hujjatlardagi ma'lum nomuvofiqliklar tuzatiladi:
  - `plan.md`'dagi `Shared<T>` → `Shared(T)`.
  - `plan.md`'dagi `alloc` qatoridagi `StringBuilder` §11'dagi `alloc.String` bilan moslashtiriladi.
  - §7.4'dagi struct literal cheklovi §14'dagi "grammar ifodalamaydigan cheklovlar" ro'yxatiga qo'shiladi.

**Qabul:** CI yashil. Runner namunaviy ijobiy va salbiy testlarni to'g'ri baholaydi.

## 1. stage0 (C11, Arche-0 → C)

Bosqich natijasi: stage0 spec'dagi Arche-0'ni to'liq qabul qiladi va stage1 source'ini compile qilishga tayyor bo'ladi.

### S1. Boshidan oxirigacha ishlaydigan eng kichik dastur

- Lexer §2 bo'yicha to'liq: barcha token'lar, literal'lar, escape'lar, `as%`, doc comment'lar, string tashqarisidagi `\r` xatosi.
- Parser eng kichik qism uchun: `import`, `fn`, method chaqiruvi, string literal, `return`.
- C emitter va runtime: `std.System`, `Output`, `Args`. Chiqishda `Output` flush qilinadi.
- `panic` built-in'i: `panic: <xabar>` source joyi bilan chiqadi va status 101.

**Qabul:** `sys.stdout.write("hello\n")` dasturi gcc va clang bilan build bo'lib ishlaydi. Lexer conformance testlari o'tadi.

### S2. Ifodalar va boshqaruv

- §14'dagi to'liq grammar uchun parser. Xato xabari fayl va qator raqamidan iborat.
- Butun son turlari, literal turi kontekstdan (§5.6), `const`, `let`/`var`, compound assignment.
- Arifmetika (§7.2): nolga bo'lish, shift kengligi va `as` trap'lari, `as%`. Precedence (§7.1) va hisoblash tartibi (§7.3).
- `if`, step'li `while`, `break`/`continue`, barcha chiqish yo'llarida `defer`, funksiyalar, rekursiya, `assert`, `unreachable`.

**Qabul:** §7 va §8 conformance testlari, trap testlari bilan birga, o'tadi.

### S3. Turlar, match va modullar

- `struct` (method'lar, associated funksiyalar, `self` turlari), payload'li `enum`, error set'lar (global error nomlari), `[N]T`, alias.
- View'lar: `&T`, `&mut T`, `[]T`, `str`. Indekslash va slicing bounds trap'i va UTF-8 chegarasi trap'i bilan.
- `match` (§9): barcha pattern'lar, copy va move bilan binding, hech biri mos kelmasa trap.
- Modullar (§3): ko'p fayl, `import … as`, import cycle'lar; dastur butunligicha compile qilinadi.
- `alloc.List(T)`: `Ast` misoli uchun kerak, shuning uchun S4 dan shu yerga olindi (`pop` S4 da, chunki u `?T` qaytaradi).
- `==` va `!=` (§5.5), struct'larda field bo'yicha.

**Qabul:** §15'dagi `Ast` misoli va ko'p modulli dastur ishlaydi. §3, §5 va §9 conformance testlari o'tadi.

### S4. Optional, xatolar va kutubxona

- `?T`, `E!T`, `try`, `catch`, `orelse`, return lifting, `some`/`none`/`ok`/`err` pattern'lari. `try` xato bilan chiqqanda ham `defer` ishlaydi.
- `main` xato qaytarsa, `error: Name` chiqadi va status 1.
- §11 runtime'i to'liq: `List.pop`, `Map` (qo'shilish tartibida), `String`, `Interner`, `Fs.read_file`/`write_file`.

**Qabul:** §15'dagi `lines` dasturi ishlaydi. stage0 o'tkazib yuborishi mumkin deb belgilanmagan barcha conformance testlari o'tadi.

## 2. Self-host (compiler Arche-0'da)

Bosqich natijasi: `compiler/` Arche-0'da yozilgan va o'zini build qiladi. Pipeline: source → CST → typed AST → SSA IR → `.ll`.

### S5. Frontend

- Lexer, LL(1) parser, error recovery, lossless CST.
- Diagnostika infratuzilmasi: barqaror xato kodlari, aniq joy, matn va JSON format, fix'lar edit sifatida.
- Parser fuzzing boshlanadi.

**Qabul:** compiler stage0 bilan build bo'ladi. CST'dan source bayt-ba-bayt tiklanadi: compiler'ning o'z source'i va barcha conformance fayllari uchun. Syntax testlari to'g'ri xato kodlari bilan o'tadi. Parser 24 soat fuzz qilinganda crash bo'lmaydi.

### S6. Nomlar va turlar

- Name resolution: modullar, import, scope, visibility.
- Arche-0 uchun type checker: lokal type inference, literal konteksti.
- §12'dagi barcha erasable tekshiruvlar, har biri o'z xato kodi bilan.

**Qabul:** Arche-0'ning barcha conformance testlari, salbiylari ham, to'g'ri xato kodi bilan o'tadi.

### S7. Memory checker

- §5.4: view qayerda turishi mumkinligi, projection, path bo'yicha exclusivity.
- §5.5: move'dan keyin foydalanish, `match`'dagi move.
- Har qoida o'z xato kodi bilan. Memory checker fuzzing boshlanadi.

**Qabul:** memory model testlari o'tadi. Compiler'ning o'z source'i memory checker'dan xatosiz o'tadi.

### S8. SSA IR va LLVM backend

- Typed AST → SSA IR lowering. Har pass'dan keyin IR verifier ishlaydi.
- IR → LLVM textual `.ll` (x86-64 Linux), executable'ni `clang` build qiladi.
- Arche-0 built-in turlari (`List`, `Map`, `String`, `Interner`) va §11'dagi `std` qismi compiler intrinsic'lari sifatida IR'ga tushiriladi. Host bilan aloqa libc'siz, to'g'ridan-to'g'ri Linux syscall'lari orqali bo'ladi.
- ABI hujjati v0: layout'lar (slice, `?T`, `E!T`, enum) va symbol mangling.
- Panic'da fayl va qator chiqadi.

**Qabul:** differential test: Arche-0 dasturlari stage0 orqali ham, yangi backend orqali ham bir xil natija beradi. Barcha run-time conformance testlari o'tadi.

### S9. Self-host

- Bootstrap skripti: stage0 → stage1 → stage2 → stage3.
- Output deterministik bo'ladi: unga vaqt, absolyut yo'llar va hash tartibi ta'sir qilmaydi.
- CI'da bootstrap har commit'da, differential test (stage1 va stage2) har kuni ishlaydi.
- stage0 muzlatiladi (git tag) va bundan keyin faqat bootstrap tekshiruvi uchun saqlanadi.

**Qabul:** toza Linux mashinada, faqat C compiler va LLVM bilan, bootstrap o'tadi va stage2 == stage3 bit-identical chiqadi.

## 3. To'liq til

Bosqich natijasi: to'liq spec bor va compiler concurrency hamda past darajadan tashqari barcha til imkoniyatlarini qo'llaydi. Compiler source'i Arche-0'da qoladi.

### S10. To'liq til spec'i

- `spec/` tuzilishi va to'liq tilning EBNF grammar'i.
- Arche-0'da yo'q imkoniyatlar bo'yicha ochiq qarorlar yopiladi: generic sintaksisi, closure'lar va ularning capture qoidalari, `for` va iterator protokoli, `if`/`match` ifoda sifatida, float semantikasi, `region` sintaksisi, `Shared(T)`.
- Memory model to'liq yoziladi: reference qochishi, region chegarasi, move'dan keyin foydalanish. Har qoidaga xato kodi biriktiriladi.

**Qabul:** spec 60 sahifadan oshmaydi. Arche-0'ning har qoidasi to'liq spec'da o'zgarishsiz turadi (strict subset).

### S11. Comptime va generics

- Comptime interpreter IR ustida ishlaydi: sandbox, I/O yo'q, qadamlar soni cheklangan.
- `comptime` parametrlar, generic tur va funksiyalar, monomorphization.
- Comptime profiler.

**Qabul:** generics va comptime conformance testlari o'tadi. Comptime fuzzing boshlanadi.

### S12. Interface'lar va iteratsiya

- `interface`, `where` constraint'lar, `dyn I` orqali dynamic dispatch.
- `for` sikli iterator protokoli orqali.

**Qabul:** interface, `where`, `dyn` va `for` conformance testlari o'tadi.

### S13. Region, unsafe va closure'lar

- `region` va region escape tekshiruvi.
- `unsafe` blok va `*T` raw pointer. Undefined behavior faqat `unsafe` ichida bo'lishi mumkin.
- Closure'lar S10'da belgilangan capture qoidalari bilan.

**Qabul:** to'liq tilning memory model testlari o'tadi, har xato o'z kodi bilan.

### S14. Qolgan til imkoniyatlari

- `if` va `match` ifoda sifatida; labeled `break`/`continue`.
- `f32`, `f64`.
- `requires`/`ensures`: debug build'da tekshiriladi.
- `nopanic` belgisi va uning tekshiruvi; error return trace.

**Qabul:** S10 spec'ining concurrency va past darajadan tashqari barcha bo'limlari implementatsiya qilingan, ularning conformance testlari o'tadi.

## 4. Kutubxonalar

### S15. `core` va `alloc`

- `core`: primitive op'lar, slice, `?T`/`E!T` helper'lari, `Eq`, `Ord`, `Hash`, `Iter`, allocator'siz fmt, math, UTF-8.
- `alloc`: `Allocator` interface; page, arena, pool va general allocator'lar; region runtime; `List`, `Map`, `Set`, `String`, `Interner`.
- Compiler intrinsic'lari kutubxona bilan almashtiriladi: Arche-0 turlari endi `alloc`'dan keladi, signature'lari §11 bilan bir xil.

**Qabul:** `core` OS'siz va allocator'siz build bo'ladi. Intrinsic'larsiz bootstrap'da ham stage2 == stage3.

### S16. Past daraja va `freestanding`

- `asm` blok, `naked` funksiyalar, `volatile` o'qish/yozish, atomic'lar va memory ordering, memory barrier'lar.
- `packed`, `align(N)`, aniq field tartibi, section va link nazorati.
- Panic handler'ni almashtirish, target ISA tanlash.
- `freestanding` target: `std`'siz build.
- Bu imkoniyatlarning hammasi `unsafe` ichida ishlaydi.

**Qabul:** faqat `core` va `alloc` bilan build bo'lgan freestanding binary QEMU'da (x86-64 va RISC-V 64) ishga tushib serial'ga yozadi. Shundan keyin arche-os ishi boshlanishi mumkin.

### S17. `std`: Linux host

- Capability turlari: `Fs`, `Net`, `Clock`, `Env`, `Proc`. Fayl, process, time, path, JSON.
- Test runner: `test` bloklari, filter, har doim chiqariladigan seed. Property-based test, seed'li random.
- Linux backend libc'siz, syscall'lar orqali ishlaydi.

**Qabul:** `std` testlari Linux'da o'tadi. Compiler §11'dagi `std` qismini shu kutubxonadan oladi, bootstrap'da stage2 == stage3.

### S18. Concurrency

- Structured scope, task'lar, task'lar orasida move, channel, `Shared(T)`.
- Testlar uchun seed'li scheduler.

**Qabul:** concurrency conformance testlari o'tadi. Seed'li scheduler bir xil seed bilan bir xil tartibni takrorlaydi.

### S19. Windows

- Windows target (`--target windows-x86_64`): LLVM orqali PE, link uchun `lld-link`, libc'siz. Runtime o'z entry point'ini beradi, Win32 import kutubxonalaridan foydalanadi va link `-Brepro` bilan takrorlanadigan bo'ladi. Target'ga bog'liq hamma narsa `compiler/target.sh`'da.
- Runtime ikkiga bo'linadi: portativ qismi `rt.c`'da qoladi, OS'ga xosi `runtime/linux.inc` va `runtime/windows.inc`'da — ikkisi ham `rt.c` boshida e'lon qilingan `os_` funksiyalarini beradi. Fiber switch Windows ABI'si bo'yicha (rdi/rsi va xmm6–15 saqlanadi), TEB'dagi stack chegaralari har switch'da yangilanadi va `__chkstk` o'zimizning bo'ladi; netpoller WSAPoll ustida.
- `arche test` har testni alohida process'da ishlatadi. Windows'da `fork` yo'q: dastur o'zini `--a0-test <raqam> <seed>` bilan qaytadan ishga tushiradi, bola chiqishini ikki faylga yozadi, ota tugagach o'qiydi.
- `std`'ning Windows backend'i: modul nomi ikkisida ham `std`, lekin fayl har host uchun alohida (`lib/std.arche`, `lib/std-windows.arche`) va compiler uni target bo'yicha tanlaydi; Win32 chaqiruvlari `lib/std/win32.arche`'da, `std.linux`'ning egizagi. Sistemalar farq qilgan joy aytib qo'yiladi: Windows'da signal yo'q (`Ending.Signaled` kelmaydi), bola bilan birga u boshlagan hammasi tugaydi (job object), `env` massivi yo'q (har nom alohida so'raladi).
- `std.SYSTEM` (`"linux"` yoki `"windows"`) va `Fs.temp_dir`: portativ dastur sistemani bilib oladi va vaqtinchalik fayllarni qayerga qo'yishni so'raydi.
- Test formati `// system: linux|windows` kalitini oladi: faqat bitta sistemada ma'noli test (masalan xom syscall) boshqasida o'tkazib yuboriladi. Conformance runner'ning compiler buyrug'i argumentli bo'lishi mumkin, chunki Windows skriptni o'zi ishga tushirmaydi (`--compiler "sh ./arche-build"`).
- Paket arxividagi yo'l hamma sistemada bir xil ma'noda bo'lishi kerak: `safe_path` `\\`, `:`, oxiri nuqta yoki bo'sh joy bilan tugaydigan qism va `CON`, `NUL` kabi qurilma nomlarini hamma joyda rad etadi, `pkg publish` ham bunday faylni nashr qilmaydi. Paket nomi identifikator, versiya esa harf, raqam, `.`, `-` va `+` bo'ladi (E0802); lock fayldagi nom ham shunday tekshiriladi. `std.path` target'ning qoidasi bilan ishlaydi: Windows'da `\\` ham ajratadi va disk nomi yo'lning bir qismi, Linux'da ular oddiy belgi. Driver cache kaliti runtime'ning hamma fayllarini (`rt.c` va `*.inc`) oladi. Buyruq qatori `PATH_UNITS` bilan cheklanmaydi (Windows chegarasi 32767).
- `.gitattributes`: repodagi matn fayllari working tree'da ham LF bo'ladi — Arche qatori LF bilan tugaydi (CR xato), `.sh` skriptlar `sh` bilan ishlaydi va bootstrap baytlarni solishtiradi.

**Qabul:** compiler va `std` testlari Windows'da o'tadi, Windows'dagi bootstrap'da stage2 == stage3. Bajarildi: `make -C compiler check` Windows'da to'liq o'tadi (458 conformance testi o'tadi, uchtasi Linux'ga xos bo'lgani uchun skip: xom syscall, SIGPIPE va `path`ning Linux qoidalari), stage0 bilan ham (`make -C conformance check`: 215 o'tdi, qolgani `stage0: skip`), `bootstrap.sh` debug, `--lib-std` va release uchun stage2 == stage3 beradi, `make -C driver check` ham o'tadi; CI'da `windows-latest` job'i shuni takrorlaydi.

## 5. Kripto, TLS, HTTP

### S20. Kripto

- SHA-256/384, HMAC, HKDF, ChaCha20-Poly1305, AES-GCM, X25519, Ed25519, ECDSA P-256, CSPRNG.
- Kod constant-time yoziladi va bu tekshiriladi.

**Qabul:** standart test vector'lar o'tadi. Constant-time tekshiruvi CI'da har commit'da ishlaydi.

### S21. TLS 1.3

- Client va server, X.509 sertifikatni parse qilish va zanjirni tekshirish.

**Qabul:** OpenSSL va curl bilan interop testlari o'tadi. TLS fuzzing boshlanadi.

### S22. HTTP

- HTTP/1.1 (keep-alive, chunked) va HTTP/2 (HPACK, stream multiplexing).

**Qabul:** curl va brauzerlar bilan interop testlari o'tadi. HTTP parser fuzzing boshlanadi.

## 6. Toolchain

### S23. Diagnostika va navigatsiya buyruqlari

- `arche check --json`, `arche fix`, `arche explain`.
- `arche fmt`: lossless CST ustida, sozlamasiz.
- `arche query`: definition, reference, type va caller'lar.
- `arche doc`: doc'dagi misollar test sifatida ishga tushadi.
- `arche test`: parallel, filter bilan; S0'dagi C11 runner'ni almashtiradi.

**Qabul:** har xato kodi uchun `explain` misoli va testi bor. `arche fmt` compiler source'ida idempotent. `arche fix` qo'llangandan keyin `arche check` toza chiqadi.

### S24. Build tizimi va edition'lar

- `arche build` (`build.arche` Arche'ning o'zida), `arche run`.
- Module darajasidagi incremental compilation va content-hash (SHA-256) cache.
- Har paketda `edition`; `arche fix --edition` kodni ko'chiradi.

**Qabul:** cache bilan qayta build toza build bilan bit-identical chiqadi. Edition ko'chirish testlari o'tadi.

### S25. Package manager va registry

- `arche pkg`: dependency qo'shish, lock, vendor.
- Manifest'da `needs: [...]`. Manifest'da e'lon qilinmagan capability'ni paket ichida ishlatish compile xatosi.
- Lockfile'da har dependency content hash bilan qayd etiladi.
- Content-addressed registry: versiyalar o'zgarmas, Ed25519 bilan imzolangan, HTTPS orqali.

**Qabul:** `needs` buzilishining salbiy testi o'tadi. Imzosi yoki hash'i mos kelmagan paket rad etiladi. Bir xil source va lockfile bit-identical output beradi.

## 7. Backend'lar

### S26. Optimizatsiya

- Inlining, SCCP, DCE, GVN, LICM, bounds-check elimination.
- Comptime isbotlagan contract'lar release build'dan olib tashlanadi.

**Qabul:** compile tezligi va runtime benchmark'lari baseline sifatida qayd etiladi. Release rejimida stage2 == stage3.

### S27. O'z x86-64 backend'i

- Instruction selection va linear scan register allocation.
- Debug build IR'dan to'g'ridan-to'g'ri, LLVM'siz chiqadi.
- Debug info: source map, stack trace, DWARF (qatorlar, funksiyalar, lokal o'zgaruvchilar).
- Host'da ELF object, link `lld` bilan.

**Qabul:** compiler va `std` testlari o'z backend'i bilan o'tadi. O'z backend'i bilan ham stage2 == stage3. `gdb`'da breakpoint, qadamlash va lokal o'zgaruvchini o'qish CI'da sinaladi.

### S28. ARM64 va RISC-V 64

- Ikkala target uchun o'z backend'i va LLVM orqali release.
- Har target'ning calling convention'i ABI hujjatiga yoziladi.
- `freestanding` ARM64'da: exception vector'lar, system register'lar `asm` orqali, `aarch64-unknown-none` linker script'i (Arche OS Trek B).

**Qabul:** cross-build'da har target'da stage2 == stage3. Testlar QEMU user-mode'da o'tadi. `freestanding/` dasturlari QEMU `virt` (aarch64) da ham ishga tushib serial'ga (PL011) yozadi va CI'da tekshiriladi.

### S29. O'z object format'i va linker

- `.ao` format: code, data, rodata, bss, relocation, symbol va debug section'lari; ABI versiyasi header'da.
- Executable format: Arche OS kernel loader shu format'ni o'qiydi.
- Static linker.
- ABI hujjati yakunlanadi, har target uchun ABI test suite yoziladi.

**Qabul:** LLVM va `lld`'siz executable chiqadi. ABI test suite har target'da o'tadi: boshqa modulda compile qilingan funksiyalar har bir layout bilan o'zaro chaqiriladi.

### S30. WASM backend

- Arche → WASM, stack codegen.

**Qabul:** compiler va `std` testlarining WASM'da ishlay oladigan qismi mavjud WASM runtime'da o'tadi.

## 8. Web va WASM

### S31. WASM runtime

- Interpreter, keyin baseline JIT.
- Host interfeys: capability'lar WASM import sifatida beriladi, modul faqat o'ziga berilgan import'larga kira oladi.
- Component Model: WIT va canonical ABI (string, list, record, variant, option, result) — Nimadir kontrakti uchun.
- Resurs chegaralari: har chaqiruvga vaqt deadline'i va linear memory uchun yuqori chegara.

**Qabul:** WASM spec testsuite'i interpreter'da ham, JIT'da ham o'tadi. Berilmagan import'ga murojaat rad etiladi. Nimadir'ning `counter`, `hello` va `store` component'lari qayta build qilinmasdan ishlaydi; `runaway` vaqt va xotira chegarasida to'xtatiladi.

### S32. Web server va framework

- Har CPU'ga bitta worker; connection'lar task sifatida structured scope ichida ishlaydi.
- Request lifecycle: accept → region ochiladi → parse → routing → handler → response → region yopiladi.
- Router (path parametrlari, method'lar), middleware zanjiri, static fayllar, gzip.
- JSON encode/decode va template'lar comptime orqali.

**Qabul:** Linux'da web server HTTPS (TLS 1.3, HTTP/2) orqali static fayl va JSON API javoblarini beradi. Request'lardan keyin xotira o'smaydi.

### S33. Storage, WASM handler'lar va benchmark

- Embedded KV store: B-tree va WAL.
- Server handler'larni WASM modul sifatida sandbox'da yuklaydi.
- Throughput va p99 latency o'lchanadi.

**Qabul:** KV store crash-recovery testlari o'tadi. WASM handler HTTPS so'rovga javob beradi. Throughput va p99 baseline'lari regressiya testlariga qo'shiladi.

## 9. Yakuniy qabul

### S34. Release

- Conformance suite taxminan 2–3k testga yetkaziladi.
- Fuzzing: parser, type checker, memory checker, comptime, TLS, HTTP parser.
- Reproducibility: ikki xil mashinada build bit-identical chiqadi.
- Benchmark regressiyasi release'ni bloklaydi.
- Tarqatish: `arche`, `arche-driver`, `arche-lsp`, `lib/` va runtime bitta versiyalangan arxivda; editor ularni `PATH` yoki `ARCHE_HOME` orqali topadi.
- Litsenziya tanlanadi va `LICENSE` fayli qo'shiladi.

**Qabul (`plan.md`'dagi yakuniy mezon):**

- bootstrap'da stage2 == stage3 barcha target'larda;
- compiler va `std` testlari barcha target'larda o'tadi;
- Linux'da Arche'da yozilgan web server HTTPS so'rovga javob beradi;
- arxiv toza mashinada ochilib, dasturni build qiladi va ishga tushiradi.

## 10. Telefon kutubxonalari

Arche OS telefonining Trek A'si (Linux kernel ustida Arche userspace) uchun. Faqat 4-bosqichga (`std`) tayanadi, shuning uchun S26–S34 bilan parallel, hatto ulardan oldin ham olib borilishi mumkin. Tilning yakuniy qabuli (S34) bu sprintni kutmaydi.

### S35. Shrift, PNG, D-Bus va `std.linux`

- TrueType parse va rasterizer (antialiasing), glyph cache; lotin va kirill.
- PNG decoder (deflate bilan).
- D-Bus client: message format, autentifikatsiya, method call va signal'lar.
- `std.linux`: DRM/KMS dumb buffer, evdev input, PID 1 sifatida ishlash (bola process'larni yig'ish).
- Shrift, PNG va D-Bus parser'lari uchun fuzzing.

**Qabul:** shrift golden image testlari har platformada bit-identical. PNG decoder PngSuite'dan o'tadi. D-Bus client `dbus-daemon` bilan interop qiladi va ModemManager'dan modem ro'yxatini oladi. QEMU aarch64'da Linux kernel ustida Arche `init` DRM framebuffer'ga matn chizadi va evdev'dan input oladi.

## 11. Editor va macOS

Karvon va boshqa editor'lar tilni LSP orqali ulaydi. S36 faqat 6-bosqichga (toolchain) tayanadi va backend'larga bog'liq emas, shuning uchun raqam tartibidan oldin, S27'dan avval bajariladi. Debugger uchun DWARF S27'da.

### S36. `arche-lsp`

- `std`'da stdin'ni o'qish.
- Compiler saqlanmagan matn bilan ishlaydi: `check` va `query` faylni diskdan emas, berilgan matndan o'qiydi.
- `arche complete <fayl> <qator>:<ustun>`: shu joyda mumkin bo'lgan nomlar, field'lar va method'lar; fayl hali to'liq parse bo'lmasa ham ishlaydi.
- `arche query` va `arche test` natijalari JSON'da ham chiqadi.
- `arche-lsp`, to'liq Arche'da (`driver/`): JSON-RPC ustida `didOpen`, `didChange`, `didClose`, `publishDiagnostics`, `definition`, `references`, `hover`, `completion`, `formatting` va fix'lar uchun `codeAction`. U compiler ichida emas: compiler Arche-0'da qoladi, unda stdin yo'q va u butun ishini bitta arena'da qiladi, shuning uchun uzoq yashaydigan server bo'la olmaydi. Har so'rovda compiler alohida process'da ishga tushadi.
- Rang berish uchun TextMate grammatikasi (`editors/`); test uning kalit so'zlarini lexer bilan solishtiradi.
- LSP xabar parser'i uchun fuzzing.

**Qabul:** CI'dagi test client `arche-lsp` bilan real sessiya o'tkazadi: saqlanmagan matndagi xato diagnostika bo'lib keladi va tuzatilgach yo'qoladi; `definition`, `references`, `hover`, `completion`, `formatting` va fix kutilgan javobni beradi. Compiler source'ida `didChange`'dan diagnostikagacha 1 soniyadan oshmaydi. stage2 == stage3 saqlanadi.

### S37. macOS

- macOS target'lari (arm64, x86-64): LLVM orqali Mach-O. arm64 S28'ga tayanadi.
- `std` va runtime'ning macOS backend'i `libSystem` ustida: macOS'da syscall barqaror ABI emas, shuning uchun Linux'dagi "libc'siz" qoida bu yerda amal qilmaydi.

**Qabul:** compiler va `std` testlari macOS'da o'tadi, stage2 == stage3. Mac mashinasi kerak (S19 kabi); S34 bu sprintni kutmaydi.
