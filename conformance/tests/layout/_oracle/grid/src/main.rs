//! The grid oracle: conformance/tests/layout/_tree_grid.arche's tree in Rust,
//! over the real taffy 0.15. It runs the same cases as the Arche tests and
//! writes their lines in the same format, so the tests' expected output is
//! taffy's own.
//!
//!   grid-oracle cases <file>   one case per line, `NAME<TAB>SPEC` (the case
//!                              language of _tree_grid.arche); each case's
//!                              lines follow a `== NAME` line (`== NAME
//!                              panic` when taffy panics on it)
//!   grid-oracle existing       the hand-written grid_*.arche cases
//!                              (src/existing.rs, made by existing.py)
//!   grid-oracle f32 <file>     lines of `BITS TEXT` written by Arche's
//!                              write_f32: those f32_text writes otherwise

use taffy::prelude::*;
use taffy::{
    compute_cached_layout, compute_grid_layout, compute_hidden_layout, compute_leaf_layout, compute_root_layout,
    round_layout, AxisStaticEdge, AxisStaticPosition, Cache, CacheTree, Contain, Direction, GridTemplateArea,
    GridTemplateAreas, GridTemplateRepetition, LayoutInput, LayoutOutput, Overflow, Point, RequestedAxis, RunMode,
    SizingMode,
};

mod existing;

/// What a leaf holds.
#[derive(Clone, Copy)]
pub enum Content {
    /// Nothing: zero.
    Empty,
    /// A box of a fixed size, as an image.
    Fixed { width: f32, height: f32 },
    /// Text `width` wide on one line, wrapping into lines `line` high.
    Text { width: f32, line: f32 },
}

/// A calc() value: px + basis * percent / 100. Aligned to 8 so that taffy can
/// tag the pointer.
#[repr(C, align(8))]
pub struct Calc {
    px: f32,
    percent: f32,
}

/// One node.
pub struct Node {
    style: Style,
    content: Content,
    children: Vec<usize>,
    hoisted: Vec<NodeId>,
    cache: Cache,
    unrounded: Layout,
    final_layout: Layout,
}

/// The tree.
pub struct TestTree {
    nodes: Vec<Node>,
    calcs: Vec<Box<Calc>>,
    computed: u32,
    measured: u32,
}

impl TestTree {
    pub fn new() -> TestTree {
        TestTree { nodes: Vec::new(), calcs: Vec::new(), computed: 0, measured: 0 }
    }

    pub fn add(&mut self, style: Style, content: Content) -> NodeId {
        let id = NodeId::from(self.nodes.len());
        self.nodes.push(Node {
            style,
            content,
            children: Vec::new(),
            hoisted: Vec::new(),
            cache: Cache::new(),
            unrounded: Layout::new(),
            final_layout: Layout::new(),
        });
        id
    }

    pub fn add_child(&mut self, parent: NodeId, child: NodeId) {
        self.nodes[usize::from(parent)].children.push(usize::from(child));
    }

    /// A calc() value that lives as long as the tree.
    pub fn calc(&mut self, px: f32, percent: f32) -> *const () {
        let b = Box::new(Calc { px, percent });
        let p = &*b as *const Calc as *const ();
        self.calcs.push(b);
        p
    }

    pub fn unrounded_of(&self, id: NodeId) -> Layout {
        self.nodes[usize::from(id)].unrounded
    }

    pub fn final_of(&self, id: NodeId) -> Layout {
        self.nodes[usize::from(id)].final_layout
    }
}

fn resolve_calc(val: *const (), basis: f32) -> f32 {
    let c = unsafe { &*(val as *const Calc) };
    c.px + basis * c.percent / 100.0
}

pub struct ChildIter<'a>(std::slice::Iter<'a, usize>);
impl Iterator for ChildIter<'_> {
    type Item = NodeId;
    fn next(&mut self) -> Option<NodeId> {
        self.0.next().copied().map(NodeId::from)
    }
}

impl TraversePartialTree for TestTree {
    type ChildIter<'a> = ChildIter<'a>;
    fn child_ids(&self, node_id: NodeId) -> ChildIter<'_> {
        ChildIter(self.nodes[usize::from(node_id)].children.iter())
    }
    fn child_count(&self, node_id: NodeId) -> usize {
        self.nodes[usize::from(node_id)].children.len()
    }
    fn get_child_id(&self, node_id: NodeId, index: usize) -> NodeId {
        NodeId::from(self.nodes[usize::from(node_id)].children[index])
    }
}

impl TraverseTree for TestTree {}

impl LayoutPartialTree for TestTree {
    type CustomIdent = String;
    type CoreContainerStyle<'a> = &'a Style;

    fn get_core_container_style(&self, node_id: NodeId) -> &Style {
        &self.nodes[usize::from(node_id)].style
    }

    fn resolve_calc_value(&self, val: *const (), basis: f32) -> f32 {
        resolve_calc(val, basis)
    }

    fn set_unrounded_layout(&mut self, node_id: NodeId, layout: &Layout) {
        self.nodes[usize::from(node_id)].unrounded = *layout;
    }

    fn compute_child_layout(&mut self, node_id: NodeId, inputs: LayoutInput) -> LayoutOutput {
        if inputs.run_mode == RunMode::PerformHiddenLayout {
            return compute_hidden_layout(self, node_id);
        }
        compute_cached_layout(self, node_id, inputs, |t, node, input| {
            t.computed += 1;
            let i = usize::from(node);
            if t.nodes[i].style.display == Display::None {
                return compute_hidden_layout(t, node);
            }
            if !t.nodes[i].children.is_empty() {
                if t.nodes[i].style.display == Display::Grid {
                    return compute_grid_layout(t, node, input);
                }
                panic!("the test tree has no container algorithm but grid");
            }
            let content = t.nodes[i].content;
            let mut count: u32 = 0;
            let output = compute_leaf_layout(input, &t.nodes[i].style, resolve_calc, |known, available| {
                count += 1;
                measure(content, known, available)
            });
            t.measured += count;
            output
        })
    }
}

impl taffy::LayoutContainingBlock for TestTree {
    type OofItemStyle<'a> = &'a Style;
    fn get_oof_item_style(&self, node_id: NodeId) -> &Style {
        &self.nodes[usize::from(node_id)].style
    }
    fn clear_hoisted_children(&mut self, node_id: NodeId) {
        self.nodes[usize::from(node_id)].hoisted.clear();
    }
    fn add_hoisted_children(&mut self, node_id: NodeId, hoisted: &[NodeId]) {
        self.nodes[usize::from(node_id)].hoisted.extend_from_slice(hoisted);
    }
}

impl CacheTree for TestTree {
    fn cache_get(&mut self, node_id: NodeId, input: &LayoutInput) -> Option<LayoutOutput> {
        self.nodes[usize::from(node_id)].cache.get(input)
    }
    fn cache_store(&mut self, node_id: NodeId, input: &LayoutInput, layout_output: LayoutOutput) {
        self.nodes[usize::from(node_id)].cache.store(input, layout_output)
    }
    fn cache_clear(&mut self, node_id: NodeId) {
        let _ = self.nodes[usize::from(node_id)].cache.clear();
    }
}

impl taffy::LayoutGridContainer for TestTree {
    type GridContainerStyle<'a> = &'a Style;
    type GridItemStyle<'a> = &'a Style;
    fn get_grid_container_style(&self, node_id: NodeId) -> &Style {
        &self.nodes[usize::from(node_id)].style
    }
    fn get_grid_child_style(&self, child_node_id: NodeId) -> &Style {
        &self.nodes[usize::from(child_node_id)].style
    }
}

impl RoundTree for TestTree {
    fn get_unrounded_layout(&self, node_id: NodeId) -> Layout {
        self.nodes[usize::from(node_id)].unrounded
    }
    fn set_final_layout(&mut self, node_id: NodeId, layout: &Layout) {
        self.nodes[usize::from(node_id)].final_layout = *layout;
    }
    fn is_out_of_flow(&self, node_id: NodeId) -> bool {
        let st = &self.nodes[usize::from(node_id)].style;
        st.display != Display::None && st.position.is_out_of_flow()
    }
    fn hoisted_child_count(&self, node_id: NodeId) -> usize {
        self.nodes[usize::from(node_id)].hoisted.len()
    }
    fn get_hoisted_child_id(&self, node_id: NodeId, index: usize) -> NodeId {
        self.nodes[usize::from(node_id)].hoisted[index]
    }
}

/// What a leaf's content measures as (_tree_grid.measure).
pub fn measure(content: Content, known: Size<Option<f32>>, available: Size<AvailableSpace>) -> Size<f32> {
    if let (Some(w), Some(h)) = (known.width, known.height) {
        return Size { width: w, height: h };
    }
    let mut width: f32 = 0.0;
    let mut height: f32 = 0.0;
    match content {
        Content::Empty => {}
        Content::Fixed { width: fw, height: fh } => {
            width = fw;
            height = fh;
        }
        Content::Text { width: tw, line } => {
            let mut room = tw;
            match available.width {
                AvailableSpace::Definite(value) => room = value,
                AvailableSpace::MinContent => room = line,
                AvailableSpace::MaxContent => {}
            }
            if let Some(w) = known.width {
                room = w;
            }
            width = tw;
            if room < tw {
                width = room;
            }
            if width <= 0.0 {
                width = 0.0;
                height = tw;
            } else {
                height = (tw / width).ceil() * line;
            }
        }
    }
    Size { width: known.width.unwrap_or(width), height: known.height.unwrap_or(height) }
}

// --- Writing, as the Arche tests write ---

/// A big unsigned integer, little-endian 32-bit limbs, for `shortest`.
#[derive(Clone)]
struct Big(Vec<u32>);

impl Big {
    fn from(v: u64) -> Big {
        let mut b = Big(vec![v as u32, (v >> 32) as u32]);
        b.trim();
        b
    }
    fn trim(&mut self) {
        while self.0.last() == Some(&0) {
            self.0.pop();
        }
    }
    fn shl(&mut self, n: i32) {
        for _ in 0..n {
            self.mul_small(2);
        }
    }
    fn mul_small(&mut self, m: u32) {
        let mut carry: u64 = 0;
        for limb in self.0.iter_mut() {
            let v = (*limb as u64) * (m as u64) + carry;
            *limb = v as u32;
            carry = v >> 32;
        }
        if carry != 0 {
            self.0.push(carry as u32);
        }
    }
    fn add(a: &Big, b: &Big) -> Big {
        let n = a.0.len().max(b.0.len());
        let mut out = Vec::with_capacity(n + 1);
        let mut carry: u64 = 0;
        for i in 0..n {
            let v = carry + *a.0.get(i).unwrap_or(&0) as u64 + *b.0.get(i).unwrap_or(&0) as u64;
            out.push(v as u32);
            carry = v >> 32;
        }
        if carry != 0 {
            out.push(carry as u32);
        }
        Big(out)
    }
    /// self -= b, for self >= b.
    fn sub(&mut self, b: &Big) {
        let mut borrow: i64 = 0;
        for i in 0..self.0.len() {
            let v = self.0[i] as i64 - *b.0.get(i).unwrap_or(&0) as i64 - borrow;
            if v < 0 {
                self.0[i] = (v + (1i64 << 32)) as u32;
                borrow = 1;
            } else {
                self.0[i] = v as u32;
                borrow = 0;
            }
        }
        self.trim();
    }
    fn cmp(a: &Big, b: &Big) -> i32 {
        if a.0.len() != b.0.len() {
            return if a.0.len() < b.0.len() { -1 } else { 1 };
        }
        for i in (0..a.0.len()).rev() {
            if a.0[i] != b.0[i] {
                return if a.0[i] < b.0[i] { -1 } else { 1 };
            }
        }
        0
    }
}

/// The Arche runtime's `shortest` (compiler/runtime/rt.c): the shortest
/// digits of f * 2^e that read back the same, the last digit rounded half
/// to even, and k with the value 0.ddd * 10^k.
fn shortest(f: u64, e: i32, p: i32, min_e: i32) -> (Vec<u8>, i32) {
    let even = f & 1 == 0;
    let hidden = 1u64 << (p - 1);
    let (mut r, mut s, mut mp, mut mm);
    if e >= 0 {
        r = Big::from(f);
        mp = Big::from(1);
        mp.shl(e);
        mm = Big::from(1);
        mm.shl(e);
        if f != hidden {
            r.shl(e + 1);
            s = Big::from(2);
        } else {
            r.shl(e + 2);
            s = Big::from(4);
            mp.shl(1);
        }
    } else {
        r = Big::from(f);
        mp = Big::from(1);
        mm = Big::from(1);
        s = Big::from(1);
        if e == min_e || f != hidden {
            r.shl(1);
            s.shl(1 - e);
        } else {
            r.shl(2);
            s.shl(2 - e);
            mp = Big::from(2);
        }
    }
    let mut bits = 0;
    let mut x = f;
    while x != 0 {
        bits += 1;
        x >>= 1;
    }
    let lg = e + bits - 1;
    let mut k = if lg >= 0 { (lg * 1233) >> 12 } else { -((-lg * 1233 + 4095) >> 12) };
    if k >= 0 {
        for _ in 0..k {
            s.mul_small(10);
        }
    } else {
        for _ in 0..(-k) {
            r.mul_small(10);
            mp.mul_small(10);
            mm.mul_small(10);
        }
    }
    loop {
        let t = Big::add(&r, &mp);
        let c = Big::cmp(&t, &s);
        if if even { c >= 0 } else { c > 0 } {
            s.mul_small(10);
            k += 1;
        } else {
            break;
        }
    }
    let mut digits = Vec::new();
    loop {
        r.mul_small(10);
        mp.mul_small(10);
        mm.mul_small(10);
        let mut d: u8 = 0;
        while Big::cmp(&r, &s) >= 0 {
            r.sub(&s);
            d += 1;
        }
        let c1 = Big::cmp(&r, &mm);
        let low = if even { c1 <= 0 } else { c1 < 0 };
        let t = Big::add(&r, &mp);
        let c2 = Big::cmp(&t, &s);
        let high = if even { c2 >= 0 } else { c2 > 0 };
        if !low && !high {
            digits.push(d);
            continue;
        }
        if low && high {
            let t2 = Big::add(&r, &r);
            let c3 = Big::cmp(&t2, &s);
            if c3 > 0 || (c3 == 0 && d & 1 == 1) {
                d += 1;
            }
        } else if high {
            d += 1;
        }
        digits.push(d);
        break;
    }
    (digits, k)
}

/// write_f32, as the Arche runtime writes an f32 (compiler/runtime/rt.c
/// a0rt_write_f32): the shortest decimal that reads back as x, `.0` on
/// whole numbers, an exponent below 1e-5 and from 1e21.
pub fn f32_text(x: f32) -> String {
    let b = x.to_bits();
    let negative = b >> 31 == 1;
    let be = ((b >> 23) & 0xFF) as i32;
    let m = (b & 0x7FFFFF) as u64;
    if be == 0xFF && m != 0 {
        return "nan".to_string();
    }
    let mut out = String::new();
    if negative {
        out.push('-');
    }
    if be == 0xFF {
        out.push_str("inf");
        return out;
    }
    let (f, e) = if be == 0 { (m, -149) } else { (m | (1 << 23), be - 150) };
    if f == 0 {
        out.push_str("0.0");
        return out;
    }
    let (d, k) = shortest(f, e, 24, -149);
    let n = d.len() as i32;
    let digit = |i: i32| (b'0' + d[i as usize]) as char;
    if k > 21 || k < -5 {
        out.push(digit(0));
        if n > 1 {
            out.push('.');
            for i in 1..n {
                out.push(digit(i));
            }
        }
        out.push('e');
        out.push_str(&(k - 1).to_string());
    } else if k <= 0 {
        out.push_str("0.");
        for _ in 0..(-k) {
            out.push('0');
        }
        for i in 0..n {
            out.push(digit(i));
        }
    } else if k >= n {
        for i in 0..n {
            out.push(digit(i));
        }
        for _ in n..k {
            out.push('0');
        }
        out.push_str(".0");
    } else {
        for i in 0..n {
            if i == k {
                out.push('.');
            }
            out.push(digit(i));
        }
    }
    out
}

fn num(out: &mut String, x: f32) {
    out.push(' ');
    out.push_str(&f32_text(x));
}

fn opt(out: &mut String, x: Option<f32>) {
    match x {
        Some(v) => num(out, v),
        None => out.push_str(" none"),
    }
}

/// _show.layout_line.
pub fn layout_line(out: &mut String, name: &str, l: Layout) {
    out.push_str(name);
    out.push_str(" at");
    num(out, l.location.x);
    num(out, l.location.y);
    out.push_str(" size");
    num(out, l.size.width);
    num(out, l.size.height);
    out.push_str(" | pad");
    num(out, l.padding.left);
    num(out, l.padding.right);
    num(out, l.padding.top);
    num(out, l.padding.bottom);
    out.push_str(" | border");
    num(out, l.border.left);
    num(out, l.border.right);
    num(out, l.border.top);
    num(out, l.border.bottom);
    out.push_str(" | margin");
    num(out, l.margin.left);
    num(out, l.margin.right);
    num(out, l.margin.top);
    num(out, l.margin.bottom);
    out.push_str(" | over");
    num(out, l.scrollable_overflow_rect.left);
    num(out, l.scrollable_overflow_rect.right);
    num(out, l.scrollable_overflow_rect.top);
    num(out, l.scrollable_overflow_rect.bottom);
    out.push_str(" | sb");
    num(out, l.scrollbar_size.width);
    num(out, l.scrollbar_size.height);
    out.push_str(" | order ");
    out.push_str(&l.order.to_string());
    out.push('\n');
}

fn edge(e: AxisStaticEdge) -> &'static str {
    match e {
        AxisStaticEdge::Start => "start",
        AxisStaticEdge::Center => "center",
        AxisStaticEdge::End => "end",
    }
}

fn safety(s: AlignmentSafety) -> &'static str {
    match s {
        AlignmentSafety::Default => "default",
        AlignmentSafety::Unsafe => "unsafe",
        AlignmentSafety::Safe => "safe",
    }
}

fn axis_pos(out: &mut String, p: AxisStaticPosition) {
    num(out, p.area.start);
    num(out, p.area.end);
    out.push(' ');
    out.push_str(edge(p.align.keyword));
    out.push(' ');
    out.push_str(safety(p.align.safety));
    out.push(' ');
    out.push_str(edge(p.align.fallback));
}

/// _tree_grid.output_lines.
pub fn output_lines(out: &mut String, name: &str, o: &LayoutOutput) {
    out.push_str(name);
    out.push_str(" out size");
    num(out, o.size.width);
    num(out, o.size.height);
    out.push_str(" | over");
    num(out, o.scrollable_overflow_rect.left);
    num(out, o.scrollable_overflow_rect.right);
    num(out, o.scrollable_overflow_rect.top);
    num(out, o.scrollable_overflow_rect.bottom);
    out.push_str(" | base");
    opt(out, o.baselines.first);
    out.push_str(" | area");
    match o.oof_positioning_area {
        Some(area) => {
            num(out, area.size.width);
            num(out, area.size.height);
            num(out, area.offset.x);
            num(out, area.offset.y);
        }
        None => out.push_str(" none"),
    }
    out.push('\n');
    for c in o.oof_candidates.iter() {
        out.push_str(name);
        out.push_str(" cand ");
        out.push_str(&u64::from(c.node).to_string());
        out.push_str(" order ");
        out.push_str(&c.order.to_string());
        if c.position == Position::Fixed {
            out.push_str(" fixed");
        } else {
            out.push_str(" absolute");
        }
        out.push_str(" x");
        axis_pos(out, c.static_position.x);
        out.push_str(" y");
        axis_pos(out, c.static_position.y);
        out.push('\n');
    }
}

/// _tree_grid.counts_line.
pub fn counts_line(out: &mut String, name: &str, t: &TestTree) {
    out.push_str(&format!("{} computed {} measured {}\n", name, t.computed, t.measured));
}

// --- The case language (_tree_grid.arche's, which documents it) ---

/// The token at pos: "(" or ")", or a word up to a space or the ")" that
/// closes its node; (start, end), empty at the end.
fn token_at(src: &str, pos: usize) -> (usize, usize) {
    let b = src.as_bytes();
    let mut at = pos;
    while at < b.len() && (b[at] == b' ' || b[at] == b'\n') {
        at += 1;
    }
    if at >= b.len() {
        return (at, at);
    }
    if b[at] == b'(' || b[at] == b')' {
        return (at, at + 1);
    }
    let mut end = at;
    let mut depth: usize = 0;
    while end < b.len() {
        let c = b[end];
        if c == b' ' || c == b'\n' {
            break;
        }
        if c == b'(' {
            depth += 1;
        } else if c == b')' {
            if depth == 0 {
                break;
            }
            depth -= 1;
        }
        end += 1;
    }
    (at, end)
}

/// The pieces of text between the separators outside parentheses and
/// brackets.
fn split_top(text: &str, sep: u8) -> Vec<&str> {
    let b = text.as_bytes();
    let mut out = Vec::new();
    let mut depth: usize = 0;
    let mut start = 0;
    for i in 0..b.len() {
        let c = b[i];
        if c == b'(' || c == b'[' {
            depth += 1;
        } else if c == b')' || c == b']' {
            depth -= 1;
        } else if c == sep && depth == 0 {
            out.push(&text[start..i]);
            start = i + 1;
        }
    }
    out.push(&text[start..]);
    out
}

fn piece(text: &str, index: usize) -> &str {
    split_top(text, b',')[index]
}

fn piece_count(text: &str) -> usize {
    split_top(text, b',').len()
}

/// The integer of the digits over a power of ten, in f64, then f32.
fn parse_f32(text: &str) -> f32 {
    let b = text.as_bytes();
    let mut i = 0;
    let mut negative = false;
    if !b.is_empty() && b[0] == b'-' {
        negative = true;
        i = 1;
    }
    let mut mantissa: u64 = 0;
    let mut scale: f64 = 1.0;
    let mut after_point = false;
    while i < b.len() {
        let c = b[i];
        i += 1;
        if c == b'.' {
            after_point = true;
            continue;
        }
        if !c.is_ascii_digit() {
            break;
        }
        mantissa = mantissa * 10 + (c - b'0') as u64;
        if after_point {
            scale *= 10.0;
        }
    }
    let value = ((mantissa as f64) / scale) as f32;
    if negative {
        -value
    } else {
        value
    }
}

fn is_percent(text: &str) -> bool {
    text.ends_with('%')
}

/// calc(PX+PERCENT%).
fn parse_calc(t: &mut TestTree, text: &str) -> *const () {
    let inner = &text[5..text.len() - 1];
    let b = inner.as_bytes();
    let mut plus = 1;
    while plus < b.len() && b[plus] != b'+' {
        plus += 1;
    }
    t.calc(parse_f32(&inner[..plus]), parse_f32(&inner[plus + 1..]))
}

fn parse_dimension(t: &mut TestTree, text: &str) -> Dimension {
    match text {
        "auto" => return Dimension::auto(),
        "content" => return Dimension::content(),
        "min-content" => return Dimension::min_content(),
        "max-content" => return Dimension::max_content(),
        "fit-content" => return Dimension::fit_content(),
        "stretch" => return Dimension::stretch(),
        _ => {}
    }
    if text.starts_with("fit-content(") {
        let inner = &text[12..text.len() - 1];
        if inner.starts_with("calc(") {
            return Dimension::fit_content_calc(parse_calc(t, inner));
        }
        if is_percent(inner) {
            return Dimension::fit_content_percent(parse_f32(inner) / 100.0);
        }
        return Dimension::fit_content_px(parse_f32(inner));
    }
    if text.starts_with("calc(") {
        return Dimension::calc(parse_calc(t, text));
    }
    if is_percent(text) {
        return Dimension::percent(parse_f32(text) / 100.0);
    }
    Dimension::length(parse_f32(text))
}

fn parse_lpa(t: &mut TestTree, text: &str) -> LengthPercentageAuto {
    if text == "auto" {
        return LengthPercentageAuto::auto();
    }
    if text.starts_with("calc(") {
        return LengthPercentageAuto::calc(parse_calc(t, text));
    }
    if is_percent(text) {
        return LengthPercentageAuto::percent(parse_f32(text) / 100.0);
    }
    LengthPercentageAuto::length(parse_f32(text))
}

fn parse_lp(t: &mut TestTree, text: &str) -> LengthPercentage {
    if text.starts_with("calc(") {
        return LengthPercentage::calc(parse_calc(t, text));
    }
    if is_percent(text) {
        return LengthPercentage::percent(parse_f32(text) / 100.0);
    }
    LengthPercentage::length(parse_f32(text))
}

fn parse_rect_lpa(t: &mut TestTree, text: &str) -> Rect<LengthPercentageAuto> {
    if piece_count(text) == 1 {
        let v = parse_lpa(t, text);
        return Rect { left: v, right: v, top: v, bottom: v };
    }
    Rect {
        left: parse_lpa(t, piece(text, 0)),
        right: parse_lpa(t, piece(text, 1)),
        top: parse_lpa(t, piece(text, 2)),
        bottom: parse_lpa(t, piece(text, 3)),
    }
}

fn parse_rect_lp(t: &mut TestTree, text: &str) -> Rect<LengthPercentage> {
    if piece_count(text) == 1 {
        let v = parse_lp(t, text);
        return Rect { left: v, right: v, top: v, bottom: v };
    }
    Rect {
        left: parse_lp(t, piece(text, 0)),
        right: parse_lp(t, piece(text, 1)),
        top: parse_lp(t, piece(text, 2)),
        bottom: parse_lp(t, piece(text, 3)),
    }
}

fn parse_safety(text: &str) -> AlignmentSafety {
    if text.starts_with("safe-") {
        AlignmentSafety::Safe
    } else if text.starts_with("unsafe-") {
        AlignmentSafety::Unsafe
    } else {
        AlignmentSafety::Default
    }
}

fn keyword_text(text: &str) -> &str {
    if let Some(rest) = text.strip_prefix("safe-") {
        rest
    } else if let Some(rest) = text.strip_prefix("unsafe-") {
        rest
    } else {
        text
    }
}

fn parse_align_items(text: &str) -> AlignItems {
    let keyword = match keyword_text(text) {
        "start" => AlignItemsKeyword::Start,
        "end" => AlignItemsKeyword::End,
        "flex-start" => AlignItemsKeyword::FlexStart,
        "flex-end" => AlignItemsKeyword::FlexEnd,
        "self-start" => AlignItemsKeyword::SelfStart,
        "self-end" => AlignItemsKeyword::SelfEnd,
        "center" => AlignItemsKeyword::Center,
        "baseline" => AlignItemsKeyword::Baseline,
        "stretch" => AlignItemsKeyword::Stretch,
        _ => AlignItemsKeyword::Normal,
    };
    AlignItems { keyword, safety: parse_safety(text) }
}

fn parse_align_content(text: &str) -> AlignContent {
    let keyword = match keyword_text(text) {
        "start" => AlignContentKeyword::Start,
        "end" => AlignContentKeyword::End,
        "flex-start" => AlignContentKeyword::FlexStart,
        "flex-end" => AlignContentKeyword::FlexEnd,
        "center" => AlignContentKeyword::Center,
        "stretch" => AlignContentKeyword::Stretch,
        "space-between" => AlignContentKeyword::SpaceBetween,
        "space-evenly" => AlignContentKeyword::SpaceEvenly,
        "space-around" => AlignContentKeyword::SpaceAround,
        _ => AlignContentKeyword::Normal,
    };
    AlignContent { keyword, safety: parse_safety(text) }
}

fn parse_overflow(text: &str) -> Overflow {
    match text {
        "clip" => Overflow::Clip,
        "hidden" => Overflow::Hidden,
        "scroll" => Overflow::Scroll,
        _ => Overflow::Visible,
    }
}

/// A track's minimum: a length, N%, calc(...), auto, min-content or
/// max-content.
fn parse_min(t: &mut TestTree, text: &str) -> MinTrackSizingFunction {
    match text {
        "auto" => return MinTrackSizingFunction::auto(),
        "min-content" => return MinTrackSizingFunction::min_content(),
        "max-content" => return MinTrackSizingFunction::max_content(),
        _ => {}
    }
    if text.starts_with("calc(") {
        return MinTrackSizingFunction::calc(parse_calc(t, text));
    }
    if is_percent(text) {
        return MinTrackSizingFunction::percent(parse_f32(text) / 100.0);
    }
    MinTrackSizingFunction::length(parse_f32(text))
}

/// A track's maximum: the minimum's values, Nfr and fit-content(...).
fn parse_max(t: &mut TestTree, text: &str) -> MaxTrackSizingFunction {
    match text {
        "auto" => return MaxTrackSizingFunction::auto(),
        "min-content" => return MaxTrackSizingFunction::min_content(),
        "max-content" => return MaxTrackSizingFunction::max_content(),
        _ => {}
    }
    if text.starts_with("fit-content(") {
        let inner = &text[12..text.len() - 1];
        if inner.starts_with("calc(") {
            return MaxTrackSizingFunction::fit_content_calc(parse_calc(t, inner));
        }
        if is_percent(inner) {
            return MaxTrackSizingFunction::fit_content_percent(parse_f32(inner) / 100.0);
        }
        return MaxTrackSizingFunction::fit_content_px(parse_f32(inner));
    }
    if text.ends_with("fr") {
        return MaxTrackSizingFunction::fr(parse_f32(text));
    }
    if text.starts_with("calc(") {
        return MaxTrackSizingFunction::calc(parse_calc(t, text));
    }
    if is_percent(text) {
        return MaxTrackSizingFunction::percent(parse_f32(text) / 100.0);
    }
    MaxTrackSizingFunction::length(parse_f32(text))
}

/// A track: minmax(MIN,MAX), Nfr (minmax(auto,Nfr)), fit-content(...)
/// (minmax(auto,fit-content(...))) or one value for both.
fn parse_track(t: &mut TestTree, text: &str) -> TrackSizingFunction {
    if text.starts_with("minmax(") {
        let inner = &text[7..text.len() - 1];
        let parts = split_top(inner, b',');
        let min = parse_min(t, parts[0]);
        let max = parse_max(t, parts[1]);
        return TrackSizingFunction { min, max };
    }
    if text.ends_with("fr") || text.starts_with("fit-content(") {
        let max = parse_max(t, text);
        return TrackSizingFunction { min: MinTrackSizingFunction::auto(), max };
    }
    let min = parse_min(t, text);
    let max = parse_max(t, text);
    TrackSizingFunction { min, max }
}

/// [a.b]: a group of line names.
fn parse_names(text: &str) -> Vec<String> {
    let inner = &text[1..text.len() - 1];
    let mut out = Vec::new();
    for name in inner.split('.') {
        if !name.is_empty() {
            out.push(name.to_string());
        }
    }
    out
}

/// Tracks and name groups: the tracks, and a name group for every line if
/// any group is written.
fn parse_tracks_with_names(t: &mut TestTree, pieces: &[&str]) -> (Vec<TrackSizingFunction>, Vec<Vec<String>>) {
    let mut tracks = Vec::new();
    let mut groups = Vec::new();
    let mut pending: Vec<String> = Vec::new();
    let mut any_names = false;
    for p in pieces {
        if p.starts_with('[') {
            any_names = true;
            pending.extend(parse_names(p));
        } else {
            groups.push(std::mem::take(&mut pending));
            tracks.push(parse_track(t, p));
        }
    }
    groups.push(pending);
    if !any_names {
        groups.clear();
    }
    (tracks, groups)
}

/// A template: tracks, repeat(COUNT,...) and name groups.
fn parse_template(t: &mut TestTree, text: &str) -> (Vec<GridTemplateComponent<String>>, Vec<Vec<String>>) {
    let pieces = split_top(text, b',');
    let mut comps = Vec::new();
    let mut groups = Vec::new();
    let mut pending: Vec<String> = Vec::new();
    let mut any_names = false;
    for p in pieces {
        if p.starts_with('[') {
            any_names = true;
            pending.extend(parse_names(p));
        } else if p.starts_with("repeat(") {
            groups.push(std::mem::take(&mut pending));
            let inner = &p[7..p.len() - 1];
            let parts = split_top(inner, b',');
            let count = match parts[0] {
                "auto-fill" => RepetitionCount::AutoFill,
                "auto-fit" => RepetitionCount::AutoFit,
                n => RepetitionCount::Count(parse_f32(n) as u16),
            };
            let (tracks, line_names) = parse_tracks_with_names(t, &parts[1..]);
            comps.push(GridTemplateComponent::Repeat(GridTemplateRepetition { count, tracks, line_names }));
        } else {
            groups.push(std::mem::take(&mut pending));
            comps.push(GridTemplateComponent::Single(parse_track(t, p)));
        }
    }
    groups.push(pending);
    if !any_names {
        groups.clear();
    }
    (comps, groups)
}

/// A placement: auto, N, span-N, NAME:N or span-NAME:N.
fn parse_placement(text: &str) -> GridPlacement<String> {
    if text == "auto" {
        return GridPlacement::Auto;
    }
    if let Some(rest) = text.strip_prefix("span-") {
        if let Some((name, n)) = rest.split_once(':') {
            return GridPlacement::NamedSpan(name.to_string(), parse_f32(n) as u16);
        }
        return GridPlacement::from_span(parse_f32(rest) as u16);
    }
    if let Some((name, n)) = text.split_once(':') {
        return GridPlacement::NamedLine(name.to_string(), parse_f32(n) as i16);
    }
    GridPlacement::from_line_index(parse_f32(text) as i16)
}

fn parse_line(text: &str) -> Line<GridPlacement<String>> {
    let parts = split_top(text, b'/');
    Line { start: parse_placement(parts[0]), end: parse_placement(parts[1]) }
}

fn apply_prop(t: &mut TestTree, style: &mut Style, content: &mut Content, prop: &str) {
    let (key, value) = prop.split_once('=').expect("a property without =");
    match key {
        "w" => style.size.width = parse_dimension(t, value),
        "h" => style.size.height = parse_dimension(t, value),
        "minw" => style.min_size.width = parse_lpa(t, value),
        "minh" => style.min_size.height = parse_lpa(t, value),
        "maxw" => style.max_size.width = parse_lpa(t, value),
        "maxh" => style.max_size.height = parse_lpa(t, value),
        "gap" => {
            if piece_count(value) == 1 {
                let g = parse_lp(t, value);
                style.gap = Size { width: g, height: g };
            } else {
                style.gap = Size { width: parse_lp(t, piece(value, 0)), height: parse_lp(t, piece(value, 1)) };
            }
        }
        "m" => style.margin = parse_rect_lpa(t, value),
        "p" => style.padding = parse_rect_lp(t, value),
        "b" => style.border = parse_rect_lp(t, value),
        "inset" => style.inset = parse_rect_lpa(t, value),
        "il" => style.inset.left = parse_lpa(t, value),
        "ir" => style.inset.right = parse_lpa(t, value),
        "it" => style.inset.top = parse_lpa(t, value),
        "ib" => style.inset.bottom = parse_lpa(t, value),
        "pos" => {
            style.position = match value {
                "relative" => Position::Relative,
                "absolute" => Position::Absolute,
                "fixed" => Position::Fixed,
                _ => Position::Static,
            }
        }
        "ai" => style.align_items = parse_align_items(value),
        "as" => style.align_self = Some(parse_align_items(value)),
        "ji" => style.justify_items = parse_align_items(value),
        "js" => style.justify_self = Some(parse_align_items(value)),
        "ac" => style.align_content = parse_align_content(value),
        "jc" => style.justify_content = parse_align_content(value),
        "ar" => style.aspect_ratio = Some(parse_f32(value)),
        "box" => {
            style.box_sizing = if value == "content" { BoxSizing::ContentBox } else { BoxSizing::BorderBox };
        }
        "dirn" => style.direction = if value == "rtl" { Direction::Rtl } else { Direction::Ltr },
        "ox" => style.overflow.x = parse_overflow(value),
        "oy" => style.overflow.y = parse_overflow(value),
        "sw" => style.scrollbar_width = parse_f32(value),
        "contain" => {
            style.contain = match value {
                "layout" => Contain::LAYOUT,
                "paint" => Contain::PAINT,
                "content" => Contain::CONTENT,
                _ => Contain::NONE,
            }
        }
        "replaced" => style.item_is_replaced = value == "1",
        "creplaced" => style.item_is_compressible_replaced = value == "1",
        "display" => style.display = if value == "none" { Display::None } else { Display::Grid },
        "cols" => {
            let (comps, names) = parse_template(t, value);
            style.grid_template_columns = comps;
            style.grid_template_column_names = names;
        }
        "rows" => {
            let (comps, names) = parse_template(t, value);
            style.grid_template_rows = comps;
            style.grid_template_row_names = names;
        }
        "acols" => {
            let pieces = split_top(value, b',');
            style.grid_auto_columns = pieces.iter().map(|p| parse_track(t, p)).collect();
        }
        "arows" => {
            let pieces = split_top(value, b',');
            style.grid_auto_rows = pieces.iter().map(|p| parse_track(t, p)).collect();
        }
        "flow" => {
            style.grid_auto_flow = match value {
                "column" => GridAutoFlow::Column,
                "row-dense" => GridAutoFlow::RowDense,
                "column-dense" => GridAutoFlow::ColumnDense,
                _ => GridAutoFlow::Row,
            }
        }
        "areas" => {
            let pieces = split_top(value, b',');
            let mut areas = Vec::new();
            for p in &pieces[2..] {
                let f = split_top(p, b':');
                areas.push(GridTemplateArea {
                    name: f[0].to_string(),
                    row_start: parse_f32(f[1]) as u16,
                    row_end: parse_f32(f[2]) as u16,
                    column_start: parse_f32(f[3]) as u16,
                    column_end: parse_f32(f[4]) as u16,
                });
            }
            style.grid_template_areas = Some(GridTemplateAreas {
                areas,
                row_count: parse_f32(pieces[0]) as u16,
                column_count: parse_f32(pieces[1]) as u16,
            });
        }
        "col" => style.grid_column = parse_line(value),
        "row" => style.grid_row = parse_line(value),
        "fixed" => *content = Content::Fixed { width: parse_f32(piece(value, 0)), height: parse_f32(piece(value, 1)) },
        "text" => *content = Content::Text { width: parse_f32(piece(value, 0)), line: parse_f32(piece(value, 1)) },
        _ => panic!("an unknown property {key}"),
    }
}

fn parse_node(t: &mut TestTree, src: &str, pos: &mut usize) -> NodeId {
    let open = token_at(src, *pos);
    assert!(&src[open.0..open.1] == "(", "a node starts with (");
    let kind = token_at(src, open.1);
    *pos = kind.1;
    let mut style = Style::default();
    if &src[kind.0..kind.1] == "grid" {
        style.display = Display::Grid;
    }
    let mut content = Content::Empty;
    loop {
        let tk = token_at(src, *pos);
        let text = &src[tk.0..tk.1];
        if text == "(" || text == ")" {
            break;
        }
        assert!(!text.is_empty(), "a node is not closed");
        *pos = tk.1;
        apply_prop(t, &mut style, &mut content, text);
    }
    let id = t.add(style, content);
    loop {
        let tk = token_at(src, *pos);
        let text = &src[tk.0..tk.1];
        if text == ")" {
            *pos = tk.1;
            break;
        }
        assert!(!text.is_empty(), "a node is not closed");
        let child = parse_node(t, src, pos);
        t.add_child(id, child);
    }
    id
}

fn parse_space(text: &str) -> AvailableSpace {
    match text {
        "min" => AvailableSpace::MinContent,
        "max" => AvailableSpace::MaxContent,
        _ => AvailableSpace::Definite(parse_f32(text)),
    }
}

fn parse_known(text: &str) -> Option<f32> {
    if text == "none" {
        None
    } else {
        Some(parse_f32(text))
    }
}

fn node_line(out: &mut String, name: &str, index: usize, l: Layout) {
    layout_line(out, &format!("{name}.{index}"), l);
}

/// _tree_grid.run.
pub fn run(out: &mut String, name: &str, spec: &str) {
    let mut t = TestTree::new();
    let mut pos = 0;
    let mut avail = Size { width: AvailableSpace::Definite(800.0), height: AvailableSpace::Definite(600.0) };
    let mut mode = 0;
    let mut do_round = false;
    let mut sizing = SizingMode::InherentSize;
    let mut known: Size<Option<f32>> = Size { width: None, height: None };
    loop {
        let tk = token_at(spec, pos);
        let text = &spec[tk.0..tk.1];
        if text == "(" {
            break;
        }
        assert!(!text.is_empty(), "a case without a node");
        pos = tk.1;
        if let Some(value) = text.strip_prefix("avail=") {
            avail = Size { width: parse_space(piece(value, 0)), height: parse_space(piece(value, 1)) };
        } else if text == "mode=layout" {
            mode = 0;
        } else if text == "mode=direct" {
            mode = 1;
        } else if text == "mode=size" {
            mode = 2;
        } else if text == "mode=sizew" {
            mode = 3;
        } else if text == "mode=sizeh" {
            mode = 4;
        } else if text == "round" {
            do_round = true;
        } else if text == "sizing=content" {
            sizing = SizingMode::ContentSize;
        } else if let Some(value) = text.strip_prefix("known=") {
            known = Size { width: parse_known(piece(value, 0)), height: parse_known(piece(value, 1)) };
        } else {
            panic!("an unknown header {text}");
        }
    }
    let root = parse_node(&mut t, spec, &mut pos);
    let count = t.nodes.len();
    if mode == 0 {
        compute_root_layout(&mut t, root, avail);
        for i in 0..count {
            node_line(out, name, i, t.unrounded_of(NodeId::from(i)));
        }
    } else if mode == 1 {
        let input = LayoutInput {
            run_mode: RunMode::PerformLayout,
            sizing_mode: sizing,
            axis: RequestedAxis::Both,
            known_dimensions: known,
            known_dimensions_are_definite: Size { width: true, height: true },
            parent_size: avail.into_options(),
            available_space: avail,
            vertical_margins_are_collapsible: Line::FALSE,
        };
        let o = t.compute_child_layout(root, input);
        output_lines(out, name, &o);
        for i in 1..count {
            node_line(out, name, i, t.unrounded_of(NodeId::from(i)));
        }
    } else {
        let axis = match mode {
            3 => RequestedAxis::Horizontal,
            4 => RequestedAxis::Vertical,
            _ => RequestedAxis::Both,
        };
        let input = LayoutInput {
            run_mode: RunMode::ComputeSize,
            sizing_mode: sizing,
            axis,
            known_dimensions: known,
            known_dimensions_are_definite: Size { width: true, height: true },
            parent_size: avail.into_options(),
            available_space: avail,
            vertical_margins_are_collapsible: Line::FALSE,
        };
        let o = t.compute_child_layout(root, input);
        out.push_str(name);
        out.push_str(" size");
        num(out, o.size.width);
        num(out, o.size.height);
        out.push('\n');
    }
    if do_round {
        round_layout(&mut t, root);
        for r in 0..count {
            node_line(out, &format!("{name}r"), r, t.final_of(NodeId::from(r)));
        }
    }
    counts_line(out, name, &t);
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut out = String::new();
    match args.get(1).map(|s| s.as_str()) {
        Some("cases") => {
            let text = std::fs::read_to_string(&args[2]).expect("the case file");
            for line in text.lines() {
                if line.trim().is_empty() {
                    continue;
                }
                let (name, spec) = line.split_once('\t').expect("NAME<TAB>SPEC");
                // A case taffy panics on is reported, not written: gen.py drops it.
                let mut lines = String::new();
                let ok = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| run(&mut lines, name, spec))).is_ok();
                if ok {
                    out.push_str(&format!("== {name}\n"));
                    out.push_str(&lines);
                } else {
                    out.push_str(&format!("== {name} panic\n"));
                }
            }
        }
        Some("existing") => existing::run_all(&mut out),
        Some("f32") => {
            // Lines of `BITS TEXT` (an Arche program's write_f32): those whose
            // text f32_text does not give.
            let text = std::fs::read_to_string(&args[2]).expect("the f32 file");
            for line in text.lines() {
                let (bits, written) = line.split_once(' ').expect("BITS TEXT");
                let mine = f32_text(f32::from_bits(bits.parse().expect("bits")));
                if mine != written {
                    out.push_str(&format!("{bits}: arche {written}, oracle {mine}\n"));
                }
            }
        }
        _ => {
            eprintln!("usage: grid-oracle cases <file> | existing | f32 <file>");
            std::process::exit(2);
        }
    }
    print!("{out}");
}
