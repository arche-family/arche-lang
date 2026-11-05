# sample

A module to document: what arche doc shows of it.

## Point

```arche
pub type Point = struct
```

A point on the plane.

## Point.moved

```arche
pub fn moved(self: Point, dx: i32, dy: i32) -> Point
```

The point moved by dx and dy.

```arche
let p = sample.Point { x: 1, y: 2 }.moved(dx: 3, dy: 4);
assert(p.x == 4 and p.y == 6);
```

## LIMIT

```arche
pub const LIMIT: i32 = 1000;
```

The largest coordinate.

## inside

```arche
pub fn inside(p: Point) -> bool
```

Whether a point is inside the limits.
