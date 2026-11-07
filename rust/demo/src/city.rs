//! The C++ city demo's socket designs and vertical constraints, expressed in Rust.
use crate::viewer::Geometry;
use glam::Vec3;
use tinywfc::{
    rotate_y, rules_from_sockets, Coordinate, Rules, Settings, Size, SocketTile, Solver, TileId,
};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    Air,
    Road,
    Canal,
    Bridge,
    Park,
    Building,
    Floor,
    FlatRoof,
    GardenRoof,
    PitchedRoof,
    Ramp,
    RampTop,
    Deck,
}
#[derive(Clone)]
pub struct Tile {
    pub kind: Kind,
    pub turns: i32,
    pub road: u8,
    pub canal: u8,
    pub pillar: bool,
}
pub struct Tileset {
    pub rules: Rules,
    pub tiles: Vec<Tile>,
    ground: Vec<TileId>,
    upper: Vec<TileId>,
    floors: Vec<TileId>,
}
#[derive(Clone, Copy, PartialEq, Eq)]
enum S {
    Ground,
    Lot,
    Road,
    Canal,
    Sky,
    House,
    Upper,
    Deck,
    Pillar0,
    Pillar1,
    Ramp0,
    Ramp1,
    Ramp2,
    Ramp3,
}
fn ground(x: S, nx: S, z: S, nz: S, top: S) -> [S; 6] {
    [x, nx, top, S::Ground, z, nz]
}
fn upper(top: S, bottom: S) -> [S; 6] {
    [S::Upper, S::Upper, top, bottom, S::Upper, S::Upper]
}
fn vertical(s: S, t: i32) -> S {
    match s {
        S::Pillar0 => {
            if t % 2 == 0 {
                S::Pillar0
            } else {
                S::Pillar1
            }
        }
        S::Ramp0 => [S::Ramp0, S::Ramp1, S::Ramp2, S::Ramp3][t as usize % 4],
        _ => s,
    }
}
fn mask(s: [S; 6], socket: S) -> u8 {
    let mut bits = 0;
    for (bit, side) in [0, 1, 4, 5].into_iter().enumerate() {
        if s[side] == socket {
            bits |= 1 << bit;
        }
    }
    bits
}
pub fn tileset() -> Tileset {
    use S::*;
    let designs = [
        (Kind::Road, ground(Lot, Lot, Road, Road, Sky), 1.6, 2),
        (Kind::Road, ground(Road, Lot, Road, Lot, Sky), 0.6, 4),
        (Kind::Road, ground(Road, Road, Road, Lot, Sky), 0.35, 4),
        (Kind::Road, ground(Road, Road, Road, Road, Sky), 0.3, 1),
        (Kind::Canal, ground(Lot, Lot, Canal, Canal, Sky), 0.5, 2),
        (Kind::Canal, ground(Canal, Lot, Canal, Lot, Sky), 0.25, 4),
        (Kind::Bridge, ground(Canal, Canal, Road, Road, Sky), 0.3, 2),
        (Kind::Park, ground(Lot, Lot, Lot, Lot, Sky), 1.8, 1),
        (Kind::Building, ground(Lot, Lot, Lot, Lot, House), 5., 1),
        (Kind::Road, ground(Road, Road, Lot, Lot, Pillar0), 0.5, 2),
        (Kind::Park, ground(Lot, Lot, Lot, Lot, Pillar0), 0.2, 2),
        (Kind::Ramp, ground(Lot, Lot, Lot, Road, Ramp0), 0.12, 4),
        (
            Kind::RampTop,
            [Upper, Upper, Sky, Ramp0, Deck, Upper],
            1.,
            4,
        ),
        (Kind::Deck, [Upper, Upper, Sky, Pillar0, Deck, Deck], 1., 2),
        (Kind::Air, upper(Sky, Sky), 1., 1),
        (Kind::Floor, upper(House, House), 1.5, 1),
        (Kind::FlatRoof, upper(Sky, House), 0.6, 1),
        (Kind::GardenRoof, upper(Sky, House), 0.25, 1),
        (Kind::PitchedRoof, upper(Sky, House), 0.35, 2),
    ];
    let mut tiles = Vec::new();
    let mut sockets = Vec::new();
    let mut ground_tiles = Vec::new();
    let mut upper_tiles = Vec::new();
    let mut floors = Vec::new();
    for (kind, s, weight, rotations) in designs {
        for turns in 0..rotations {
            let mut s = rotate_y(s, turns);
            s[2] = vertical(s[2], turns);
            s[3] = vertical(s[3], turns);
            let id = tiles.len() as u32;
            tiles.push(Tile {
                kind,
                turns,
                road: mask(s, Road),
                canal: mask(s, Canal),
                pillar: s[2] == Pillar0 || s[2] == Pillar1,
            });
            sockets.push(SocketTile {
                sockets: s,
                weight: weight / rotations as f32,
            });
            if s[3] == Ground {
                ground_tiles.push(id);
            } else {
                upper_tiles.push(id);
            }
            if kind == Kind::Floor {
                floors.push(id);
            }
        }
    }
    Tileset {
        rules: rules_from_sockets(&sockets).unwrap(),
        tiles,
        ground: ground_tiles,
        upper: upper_tiles,
        floors,
    }
}
pub fn solver(set: &Tileset, size: Size, seed: u64) -> Result<Solver, tinywfc::Error> {
    if size.y < 2 {
        return Err(tinywfc::Error::InvalidSize);
    }
    let mut solver = Solver::new(
        &set.rules,
        Settings {
            size,
            seed,
            max_backtracks: 128,
            ..Settings::default()
        },
    )?;
    for y in 0..size.y {
        for z in 0..size.z {
            for x in 0..size.x {
                let c = Coordinate::new(x, y, z);
                solver.set_priority(c, y as f64 * 100.)?;
                solver.restrict(c, if y == 0 { &set.ground } else { &set.upper })?;
                if y == size.y - 1 {
                    for &tile in &set.floors {
                        solver.ban(c, tile)?;
                    }
                }
            }
        }
    }
    Ok(solver)
}
const ASPHALT: [f32; 3] = [0.20, 0.25, 0.31];
const STONE: [f32; 3] = [0.49, 0.55, 0.57];
const WATER: [f32; 3] = [0.11, 0.46, 0.65];
const GRASS: [f32; 3] = [0.22, 0.53, 0.37];
fn cube(g: &mut Geometry, base: Vec3, a: [f32; 3], b: [f32; 3], color: [f32; 3]) {
    g.cuboid(
        base + Vec3::from_array(a),
        base + Vec3::from_array(b),
        color,
    );
}
fn tree(g: &mut Geometry, base: Vec3, scale: f32) {
    cube(
        g,
        base,
        [-0.025, 0., -0.025],
        [0.025, 0.25, 0.025],
        [0.4, 0.25, 0.15],
    );
    for i in 0..8 {
        let a = std::f32::consts::TAU * i as f32 / 8.;
        let b = std::f32::consts::TAU * (i + 1) as f32 / 8.;
        g.triangle(
            [
                base + Vec3::new(a.cos() * 0.17 * scale, 0.17, a.sin() * 0.17 * scale),
                base + Vec3::new(b.cos() * 0.17 * scale, 0.17, b.sin() * 0.17 * scale),
                base + Vec3::Y * (0.17 + 0.45 * scale),
            ],
            [0.15, 0.39, 0.27],
        );
    }
}
fn road(g: &mut Geometry, base: Vec3, sides: u8, y: f32, color: [f32; 3], markings: bool) {
    cube(g, base, [-0.29, y, -0.29], [0.29, y + 0.025, 0.29], color);
    for (bit, (dx, dz)) in [(1, (1., 0.)), (2, (-1., 0.)), (4, (0., 1.)), (8, (0., -1.))] {
        if sides & bit == 0 {
            continue;
        }
        let min = [
            if dx < 0. { -0.5 } else { -0.29 },
            y,
            if dz < 0. { -0.5 } else { -0.29 },
        ];
        let max = [
            if dx > 0. { 0.5 } else { 0.29 },
            y + 0.025,
            if dz > 0. { 0.5 } else { 0.29 },
        ];
        cube(g, base, min, max, color);
        if markings {
            let center = base + Vec3::new(dx * 0.39, y + 0.028, dz * 0.39);
            let along = Vec3::new(dx * 0.06, 0., dz * 0.06);
            g.line(center - along, center + along, [0.95, 0.84, 0.51]);
        }
    }
}
pub fn geometry(set: &Tileset, solver: &Solver, show_open: bool) -> Geometry {
    let size = solver.size();
    let mut g = Geometry::default();
    let offset = Vec3::new(-(size.x - 1) as f32 * 0.5, 0., -(size.z - 1) as f32 * 0.5);
    g.grid((size.x.max(size.z) / 2 + 2).max(5), -0.12, 1.);
    for cell in 0..solver.cell_count() {
        let c = solver.coordinate(cell).unwrap();
        let base = offset + Vec3::new(c.x as f32, c.y as f32 * 0.65, c.z as f32);
        let Some(id) = solver.tile(c) else {
            if show_open && c.y == 0 {
                cube(
                    &mut g,
                    base,
                    [-0.45, -0.06, -0.45],
                    [0.45, -0.02, 0.45],
                    [0.10, 0.18, 0.25],
                );
            }
            continue;
        };
        let tile = &set.tiles[id as usize];
        if c.y == 0 {
            cube(
                &mut g,
                base,
                [-0.49, -0.07, -0.49],
                [0.49, -0.01, 0.49],
                STONE,
            );
        }
        match tile.kind {
            Kind::Air => {}
            Kind::Road => {
                road(&mut g, base, tile.road, 0., ASPHALT, true);
            }
            Kind::Canal => {
                road(&mut g, base, tile.canal, -0.005, WATER, false);
            }
            Kind::Bridge => {
                road(&mut g, base, tile.canal, -0.005, WATER, false);
                road(&mut g, base, tile.road, 0.06, ASPHALT, true);
            }
            Kind::Park => {
                cube(&mut g, base, [-0.44, 0., -0.44], [0.44, 0.025, 0.44], GRASS);
                tree(&mut g, base + Vec3::new(-0.18, 0.025, 0.1), 0.8);
                if !tile.pillar {
                    tree(&mut g, base + Vec3::new(0.2, 0.025, -0.16), 0.65);
                }
            }
            Kind::Building | Kind::Floor => {
                let tint = ((c.x * 17 + c.z * 31).rem_euclid(5)) as usize;
                let color = [
                    [0.77, 0.61, 0.47],
                    [0.63, 0.72, 0.76],
                    [0.79, 0.73, 0.62],
                    [0.63, 0.72, 0.62],
                    [0.72, 0.57, 0.52],
                ][tint];
                cube(&mut g, base, [-0.40, 0., -0.40], [0.40, 0.65, 0.40], color);
                for x in [-0.22, 0.18] {
                    for z in [-0.405, 0.405] {
                        cube(
                            &mut g,
                            base,
                            [x, 0.22, z - 0.004],
                            [x + 0.12, 0.43, z + 0.004],
                            [0.16, 0.26, 0.35],
                        );
                    }
                }
                for z in [-0.22, 0.18] {
                    for x in [-0.405, 0.405] {
                        cube(
                            &mut g,
                            base,
                            [x - 0.004, 0.22, z],
                            [x + 0.004, 0.43, z + 0.12],
                            [0.16, 0.26, 0.35],
                        );
                    }
                }
                if tile.kind == Kind::Building {
                    cube(
                        &mut g,
                        base,
                        [-0.08, 0., 0.402],
                        [0.08, 0.27, 0.409],
                        [0.31, 0.22, 0.16],
                    );
                }
            }
            Kind::FlatRoof | Kind::GardenRoof => {
                cube(
                    &mut g,
                    base,
                    [-0.44, 0., -0.44],
                    [0.44, 0.08, 0.44],
                    if tile.kind == Kind::GardenRoof {
                        GRASS
                    } else {
                        [0.46, 0.50, 0.55]
                    },
                );
                if tile.kind == Kind::GardenRoof {
                    tree(&mut g, base + Vec3::new(0., 0.08, 0.), 0.65);
                }
            }
            Kind::PitchedRoof => {
                let rotate = |p: Vec3| {
                    if tile.turns % 2 == 0 {
                        p
                    } else {
                        Vec3::new(p.z, p.y, -p.x)
                    }
                };
                let p = [
                    Vec3::new(-0.45, 0., -0.45),
                    Vec3::new(0.45, 0., -0.45),
                    Vec3::new(-0.45, 0., 0.45),
                    Vec3::new(0.45, 0., 0.45),
                    Vec3::new(-0.45, 0.34, 0.),
                    Vec3::new(0.45, 0.34, 0.),
                ]
                .map(|p| base + rotate(p));
                for [a, b, c] in [
                    [0, 1, 5],
                    [0, 5, 4],
                    [2, 4, 5],
                    [2, 5, 3],
                    [0, 4, 2],
                    [1, 3, 5],
                ] {
                    g.triangle([p[a], p[b], p[c]], [0.69, 0.30, 0.23]);
                }
            }
            Kind::Deck => {
                let sides = if tile.turns % 2 == 0 { 12 } else { 3 };
                road(&mut g, base, sides, 0., [0.46, 0.51, 0.56], true);
            }
            Kind::Ramp | Kind::RampTop => {
                let rotate = |p: Vec3| {
                    let t = tile.turns as f32 * std::f32::consts::FRAC_PI_2;
                    Vec3::new(
                        p.x * t.cos() + p.z * t.sin(),
                        p.y,
                        -p.x * t.sin() + p.z * t.cos(),
                    )
                };
                let (low, high) = if tile.kind == Kind::Ramp {
                    (0., 0.65)
                } else {
                    (0., 0.02)
                };
                let p = [
                    [-0.28, low, -0.5],
                    [0.28, low, -0.5],
                    [0.28, high, 0.5],
                    [-0.28, high, 0.5],
                ]
                .map(|p| base + rotate(Vec3::from_array(p)));
                g.quad(p, ASPHALT);
            }
        }
        if tile.pillar {
            for x in [-0.33, 0.33] {
                cube(
                    &mut g,
                    base,
                    [x - 0.045, 0., -0.045],
                    [x + 0.045, 0.65, 0.045],
                    [0.59, 0.64, 0.68],
                );
            }
        }
    }
    g
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn cities_solve_with_ground_and_closed_roofs() {
        let set = tileset();
        for seed in [0, 3, 42] {
            let mut s = solver(&set, Size::new(8, 5, 8), seed).unwrap();
            s.run().unwrap();
            for cell in 0..s.cell_count() {
                let c = s.coordinate(cell).unwrap();
                let id = s.tile(c).unwrap();
                if c.y == 0 {
                    assert!(set.ground.contains(&id));
                } else {
                    assert!(set.upper.contains(&id));
                }
                if c.y == 4 {
                    assert!(!set.floors.contains(&id));
                }
            }
            assert!(geometry(&set, &s, false).triangle_count() > 100);
        }
    }
}
