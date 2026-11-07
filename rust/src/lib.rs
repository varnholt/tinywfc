//! Dependency-free tiled wave function collapse on 2D and 3D grids.
//!
//! ```
//! use tinywfc::{Rules, Settings, Size, Solver};
//! let mut rules = Rules::new(2)?;
//! rules.allow_everywhere(0, 1)?;
//! let mut solver = Solver::new(&rules, Settings {
//!     size: Size::new(8, 1, 8), seed: 42, ..Settings::default()
//! })?;
//! solver.run()?;
//! assert_eq!(solver.result().unwrap().len(), 64);
//! # Ok::<(), tinywfc::Error>(())
//! ```
use std::cmp::Ordering;
use std::collections::{BTreeMap, BinaryHeap};
use std::fmt;
use std::sync::Arc;

pub type TileId = u32;
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum Direction {
    PositiveX,
    NegativeX,
    PositiveY,
    NegativeY,
    PositiveZ,
    NegativeZ,
}
pub const DIRECTIONS: [Direction; 6] = [
    Direction::PositiveX,
    Direction::NegativeX,
    Direction::PositiveY,
    Direction::NegativeY,
    Direction::PositiveZ,
    Direction::NegativeZ,
];
impl Direction {
    pub const fn opposite(self) -> Self {
        DIRECTIONS[self as usize ^ 1]
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Size {
    pub x: i32,
    pub y: i32,
    pub z: i32,
}
impl Size {
    pub const fn new(x: i32, y: i32, z: i32) -> Self {
        Self { x, y, z }
    }
    pub fn cell_count(self) -> Option<usize> {
        if self.x < 1 || self.y < 1 || self.z < 1 {
            return None;
        }
        (self.x as usize)
            .checked_mul(self.y as usize)?
            .checked_mul(self.z as usize)
    }
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Coordinate {
    pub x: i32,
    pub y: i32,
    pub z: i32,
}
impl Coordinate {
    pub const fn new(x: i32, y: i32, z: i32) -> Self {
        Self { x, y, z }
    }
}
pub fn adjacent(mut c: Coordinate, d: Direction) -> Coordinate {
    match d {
        Direction::PositiveX => c.x += 1,
        Direction::NegativeX => c.x -= 1,
        Direction::PositiveY => c.y += 1,
        Direction::NegativeY => c.y -= 1,
        Direction::PositiveZ => c.z += 1,
        Direction::NegativeZ => c.z -= 1,
    };
    c
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    InvalidSize,
    InvalidRules,
    InvalidCoordinate,
    InvalidTile,
    InvalidPriority,
    Contradiction,
    CapacityExceeded,
}
impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(match self {
            Self::InvalidSize => "invalid grid dimensions",
            Self::InvalidRules => "rules need 1..=65534 tiles and positive finite weights",
            Self::InvalidCoordinate => "coordinate outside grid",
            Self::InvalidTile => "tile outside rules",
            Self::InvalidPriority => "priority must be finite",
            Self::Contradiction => "no tiling found within search limits",
            Self::CapacityExceeded => "requested allocation is too large",
        })
    }
}
impl std::error::Error for Error {}
#[derive(Clone, Debug)]
pub struct Rules {
    weights: Vec<f64>,
    allowed: Vec<bool>,
}
impl Rules {
    pub fn new(count: usize) -> Result<Self, Error> {
        if count == 0 || count > 65534 {
            return Err(Error::InvalidRules);
        }
        let size = count
            .checked_mul(count)
            .and_then(|n| n.checked_mul(6))
            .ok_or(Error::CapacityExceeded)?;
        let allowed = filled(size, false)?;
        Ok(Self {
            weights: vec![1.; count],
            allowed,
        })
    }
    pub fn tile_count(&self) -> usize {
        self.weights.len()
    }
    fn slot(&self, a: TileId, d: Direction, b: TileId) -> Result<usize, Error> {
        if a as usize >= self.tile_count() || b as usize >= self.tile_count() {
            Err(Error::InvalidTile)
        } else {
            Ok((a as usize * 6 + d as usize) * self.tile_count() + b as usize)
        }
    }
    pub fn set_weight(&mut self, tile: TileId, weight: f32) -> Result<(), Error> {
        if !weight.is_finite() || weight <= 0. {
            return Err(Error::InvalidRules);
        }
        *self
            .weights
            .get_mut(tile as usize)
            .ok_or(Error::InvalidTile)? = f64::from(weight);
        Ok(())
    }
    pub fn weight(&self, tile: TileId) -> Option<f64> {
        self.weights.get(tile as usize).copied()
    }
    /// Allow the pair in both reciprocal directions.
    pub fn allow(&mut self, a: TileId, d: Direction, b: TileId) -> Result<(), Error> {
        let i = self.slot(a, d, b)?;
        let j = self.slot(b, d.opposite(), a)?;
        self.allowed[i] = true;
        self.allowed[j] = true;
        Ok(())
    }
    pub fn allow_everywhere(&mut self, a: TileId, b: TileId) -> Result<(), Error> {
        for d in DIRECTIONS {
            self.allow(a, d, b)?;
        }
        Ok(())
    }
    pub fn allows(&self, a: TileId, d: Direction, b: TileId) -> bool {
        self.slot(a, d, b).map(|i| self.allowed[i]).unwrap_or(false)
    }
}
#[derive(Clone, Debug)]
pub struct SocketTile<S> {
    pub sockets: [S; 6],
    pub weight: f32,
}
pub fn rules_from_sockets<S: PartialEq>(tiles: &[SocketTile<S>]) -> Result<Rules, Error> {
    let mut r = Rules::new(tiles.len())?;
    for (a, t) in tiles.iter().enumerate() {
        r.set_weight(a as u32, t.weight)?;
        for (b, u) in tiles.iter().enumerate() {
            for d in DIRECTIONS {
                if t.sockets[d as usize] == u.sockets[d.opposite() as usize] {
                    r.allow(a as u32, d, b as u32)?;
                }
            }
        }
    }
    Ok(r)
}
pub fn rotate_y<S: Clone>(mut sockets: [S; 6], quarter_turns: i32) -> [S; 6] {
    for _ in 0..quarter_turns.rem_euclid(4) {
        let old = sockets.clone();
        sockets[5] = old[0].clone();
        sockets[1] = old[5].clone();
        sockets[4] = old[1].clone();
        sockets[0] = old[4].clone();
    }
    sockets
}
#[derive(Clone, Debug)]
pub struct Settings {
    pub size: Size,
    pub seed: u64,
    pub periodic: bool,
    pub max_backtracks: u32,
    pub max_restarts: u32,
    /// Parallel restart workers; 0 chooses available CPUs. WASM uses one worker.
    pub threads: usize,
}
impl Default for Settings {
    fn default() -> Self {
        Self {
            size: Size::new(8, 1, 8),
            seed: 0,
            periodic: false,
            max_backtracks: 10000,
            max_restarts: 10,
            threads: 1,
        }
    }
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Statistics {
    pub decisions: u64,
    pub backtracks: u64,
    pub restarts: u32,
}
#[derive(Clone)]
struct Random([u64; 4]);
impl Random {
    fn new(mut seed: u64) -> Self {
        Self(std::array::from_fn(|_| {
            seed = seed.wrapping_add(0x9e3779b97f4a7c15);
            let mut m = seed;
            m = (m ^ (m >> 30)).wrapping_mul(0xbf58476d1ce4e5b9);
            m = (m ^ (m >> 27)).wrapping_mul(0x94d049bb133111eb);
            m ^ (m >> 31)
        }))
    }
    fn unit(&mut self) -> f64 {
        let s = &mut self.0;
        let result = s[1].wrapping_mul(5).rotate_left(7).wrapping_mul(9);
        let shifted = s[1] << 17;
        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];
        s[2] ^= shifted;
        s[3] = s[3].rotate_left(45);
        (result >> 11) as f64 * (1.0 / 9007199254740992.0)
    }
}
struct Class {
    direction: usize,
    members: Vec<usize>,
    size: u16,
}
struct Tables {
    n: usize,
    words: usize,
    cells: usize,
    weights: Vec<f64>,
    logs: Vec<f64>,
    neighbors: Vec<[Option<usize>; 6]>,
    classes: Vec<Class>,
    affected: Vec<Vec<usize>>,
}
#[derive(Clone)]
struct State {
    domains: Vec<u64>,
    counts: Vec<usize>,
    sums: Vec<f64>,
    logs: Vec<f64>,
    supports: Vec<u16>,
    collapsed: usize,
}
#[derive(Clone, Copy)]
struct Decision {
    cell: usize,
    tile: usize,
    trail: usize,
}
#[derive(Clone, Copy, PartialEq)]
struct Entry {
    key: f64,
    cell: usize,
}
impl Eq for Entry {}
impl PartialOrd for Entry {
    fn partial_cmp(&self, b: &Self) -> Option<Ordering> {
        Some(self.cmp(b))
    }
}
impl Ord for Entry {
    fn cmp(&self, b: &Self) -> Ordering {
        b.key
            .total_cmp(&self.key)
            .then_with(|| b.cell.cmp(&self.cell))
    }
}
type CollapseCallback = Arc<dyn Fn(Coordinate, TileId) + Send + Sync>;
type ResetCallback = Arc<dyn Fn() + Send + Sync>;
/// A cloneable solver. Clones share immutable rule tables and copy the search state.
#[derive(Clone)]
pub struct Solver {
    settings: Settings,
    tables: Arc<Tables>,
    state: State,
    initial: Option<State>,
    consistent: bool,
    failed: bool,
    trail: Vec<(usize, usize)>,
    pending: Vec<(usize, usize)>,
    decisions: Vec<Decision>,
    heap: BinaryHeap<Entry>,
    noise: Vec<f64>,
    priorities: Vec<f64>,
    random: Random,
    statistics: Statistics,
    streak: u32,
    depth: usize,
    collapse_callback: Option<CollapseCallback>,
    reset_callback: Option<ResetCallback>,
}
fn filled<T: Clone>(len: usize, value: T) -> Result<Vec<T>, Error> {
    let mut v = Vec::new();
    v.try_reserve_exact(len)
        .map_err(|_| Error::CapacityExceeded)?;
    v.resize(len, value);
    Ok(v)
}
fn coordinate(size: Size, cell: usize) -> Coordinate {
    let layer = size.x as usize * size.z as usize;
    Coordinate::new(
        (cell % size.x as usize) as i32,
        (cell / layer) as i32,
        ((cell % layer) / size.x as usize) as i32,
    )
}
fn index(size: Size, c: Coordinate) -> Option<usize> {
    if c.x < 0 || c.y < 0 || c.z < 0 || c.x >= size.x || c.y >= size.y || c.z >= size.z {
        None
    } else {
        Some((c.y as usize * size.z as usize + c.z as usize) * size.x as usize + c.x as usize)
    }
}
impl Solver {
    pub fn new(rules: &Rules, settings: Settings) -> Result<Self, Error> {
        let cells = settings
            .size
            .cell_count()
            .filter(|&n| n < i32::MAX as usize)
            .ok_or(Error::InvalidSize)?;
        let n = rules.tile_count();
        let mut classes = Vec::new();
        let mut affected = vec![Vec::new(); n * 6];
        for d in DIRECTIONS {
            let mut lookup = BTreeMap::new();
            for a in 0..n {
                let list: Vec<usize> = (0..n)
                    .filter(|&b| rules.allows(a as u32, d, b as u32))
                    .collect();
                let id = *lookup.entry(list.clone()).or_insert_with(|| {
                    let id = classes.len();
                    classes.push(Class {
                        direction: d as usize,
                        members: Vec::new(),
                        size: list.len() as u16,
                    });
                    for &b in &list {
                        affected[b * 6 + d.opposite() as usize].push(id);
                    }
                    id
                });
                classes[id].members.push(a);
            }
        }
        let mut neighbors = filled(cells, [None; 6])?;
        for (cell, neighbors) in neighbors.iter_mut().enumerate() {
            for d in DIRECTIONS {
                let mut c = adjacent(coordinate(settings.size, cell), d);
                let size = settings.size;
                let axis = match d {
                    Direction::PositiveX | Direction::NegativeX => size.x,
                    Direction::PositiveY | Direction::NegativeY => size.y,
                    _ => size.z,
                };
                if index(size, c).is_none() && settings.periodic && axis > 1 {
                    c.x = c.x.rem_euclid(size.x);
                    c.y = c.y.rem_euclid(size.y);
                    c.z = c.z.rem_euclid(size.z);
                }
                neighbors[d as usize] = index(size, c);
            }
        }
        let logs: Vec<f64> = rules.weights.iter().map(|w| w * w.ln()).collect();
        let mut supports = filled(
            cells
                .checked_mul(classes.len())
                .ok_or(Error::CapacityExceeded)?,
            0,
        )?;
        for (cell, nb) in neighbors.iter().enumerate() {
            for (i, c) in classes.iter().enumerate() {
                supports[cell * classes.len() + i] = if nb[c.direction].is_some() {
                    c.size
                } else {
                    u16::MAX
                };
            }
        }
        let words = n.div_ceil(64);
        let mut domains = filled(
            cells.checked_mul(words).ok_or(Error::CapacityExceeded)?,
            u64::MAX,
        )?;
        if n % 64 != 0 {
            for cell in 0..cells {
                domains[cell * words + words - 1] = (1u64 << (n % 64)) - 1;
            }
        }
        let state = State {
            domains,
            counts: filled(cells, n)?,
            sums: filled(cells, rules.weights.iter().sum())?,
            logs: filled(cells, logs.iter().sum())?,
            supports,
            collapsed: if n == 1 { cells } else { 0 },
        };
        let priorities = filled(cells, 0.)?;
        let random = Random::new(settings.seed);
        Ok(Self {
            settings,
            tables: Arc::new(Tables {
                n,
                words,
                cells,
                weights: rules.weights.clone(),
                logs,
                neighbors,
                classes,
                affected,
            }),
            state,
            initial: None,
            consistent: true,
            failed: false,
            trail: Vec::new(),
            pending: Vec::new(),
            decisions: Vec::new(),
            heap: BinaryHeap::new(),
            noise: Vec::new(),
            priorities,
            random,
            statistics: Statistics::default(),
            streak: 0,
            depth: 0,
            collapse_callback: None,
            reset_callback: None,
        })
    }
    pub fn size(&self) -> Size {
        self.settings.size
    }
    pub fn settings(&self) -> &Settings {
        &self.settings
    }
    pub fn statistics(&self) -> Statistics {
        self.statistics
    }
    pub fn cell_count(&self) -> usize {
        self.tables.cells
    }
    pub fn tile_count(&self) -> usize {
        self.tables.n
    }
    pub fn contains(&self, c: Coordinate) -> bool {
        self.cell_index(c).is_some()
    }
    pub fn cell_index(&self, c: Coordinate) -> Option<usize> {
        index(self.size(), c)
    }
    pub fn coordinate(&self, cell: usize) -> Option<Coordinate> {
        (cell < self.cell_count()).then(|| coordinate(self.size(), cell))
    }
    pub fn possibility_count(&self, c: Coordinate) -> usize {
        self.cell_index(c)
            .map(|i| self.state.counts[i])
            .unwrap_or(0)
    }
    pub fn is_possible(&self, c: Coordinate, tile: TileId) -> bool {
        self.cell_index(c)
            .is_some_and(|i| (tile as usize) < self.tile_count() && self.has(i, tile as usize))
    }
    pub fn tile(&self, c: Coordinate) -> Option<TileId> {
        let i = self.cell_index(c)?;
        (self.state.counts[i] == 1).then(|| self.first(i) as TileId)
    }
    pub fn collapsed_count(&self) -> usize {
        self.state.collapsed
    }
    pub fn finished(&self) -> bool {
        self.state.collapsed == self.cell_count()
    }
    /// `None` until every cell has exactly one tile. Cells are x-fastest, then z, then y.
    pub fn result(&self) -> Option<Vec<TileId>> {
        self.finished().then(|| {
            (0..self.cell_count())
                .map(|i| self.first(i) as TileId)
                .collect()
        })
    }
    pub fn set_collapse_callback(
        &mut self,
        callback: impl Fn(Coordinate, TileId) + Send + Sync + 'static,
    ) {
        self.collapse_callback = Some(Arc::new(callback));
    }
    pub fn set_reset_callback(&mut self, callback: impl Fn() + Send + Sync + 'static) {
        self.reset_callback = Some(Arc::new(callback));
    }
    pub fn set_priority(&mut self, c: Coordinate, priority: f64) -> Result<(), Error> {
        let i = self.cell_index(c).ok_or(Error::InvalidCoordinate)?;
        if !priority.is_finite() {
            return Err(Error::InvalidPriority);
        }
        self.priorities[i] = priority;
        if !self.noise.is_empty() {
            self.rebuild_heap();
        }
        Ok(())
    }
    /// Constraints before the first step survive restarts; later changes may be backtracked.
    pub fn restrict(&mut self, c: Coordinate, allowed: &[TileId]) -> Result<(), Error> {
        let cell = self.cell_index(c).ok_or(Error::InvalidCoordinate)?;
        if allowed.iter().any(|&t| t as usize >= self.tile_count()) {
            return Err(Error::InvalidTile);
        }
        for t in 0..self.tile_count() {
            if !allowed.contains(&(t as u32)) && !self.remove(cell, t) {
                self.pending.clear();
                self.failed = true;
                return Err(Error::Contradiction);
            }
        }
        if self.propagate() {
            Ok(())
        } else {
            self.failed = true;
            Err(Error::Contradiction)
        }
    }
    pub fn ban(&mut self, c: Coordinate, tile: TileId) -> Result<(), Error> {
        let i = self.cell_index(c).ok_or(Error::InvalidCoordinate)?;
        if tile as usize >= self.tile_count() {
            return Err(Error::InvalidTile);
        }
        if self.remove(i, tile as usize) && self.propagate() {
            Ok(())
        } else {
            self.pending.clear();
            self.failed = true;
            Err(Error::Contradiction)
        }
    }
    fn has(&self, cell: usize, tile: usize) -> bool {
        self.state.domains[cell * self.tables.words + tile / 64] & (1u64 << (tile % 64)) != 0
    }
    fn first(&self, cell: usize) -> usize {
        (0..self.tile_count())
            .find(|&t| self.has(cell, t))
            .unwrap_or(0)
    }
    fn key(&self, cell: usize) -> f64 {
        let sum = self.state.sums[cell];
        self.priorities[cell] + sum.ln() - self.state.logs[cell] / sum + self.noise[cell]
    }
    fn push_heap(&mut self, cell: usize) {
        if self.noise.is_empty() {
            return;
        }
        if self.heap.len() > 8 * self.cell_count() + 64 {
            self.rebuild_heap();
        } else {
            self.heap.push(Entry {
                key: self.key(cell),
                cell,
            });
        }
    }
    fn rebuild_heap(&mut self) {
        self.heap = (0..self.cell_count())
            .filter(|&c| self.state.counts[c] > 1)
            .map(|cell| Entry {
                key: self.key(cell),
                cell,
            })
            .collect();
    }
    fn remove(&mut self, cell: usize, tile: usize) -> bool {
        if !self.has(cell, tile) {
            return true;
        }
        let t = &self.tables;
        self.state.domains[cell * t.words + tile / 64] &= !(1u64 << (tile % 64));
        self.state.counts[cell] -= 1;
        self.state.sums[cell] -= t.weights[tile];
        self.state.logs[cell] -= t.logs[tile];
        if self.initial.is_some() && self.settings.max_backtracks > 0 {
            self.trail.push((cell, tile));
        }
        for d in 0..6 {
            if let Some(next) = t.neighbors[cell][d] {
                for &class in &t.affected[tile * 6 + d] {
                    let count = &mut self.state.supports[next * t.classes.len() + class];
                    *count -= 1;
                    if *count == 0 {
                        for &other in &t.classes[class].members {
                            if self.state.domains[next * t.words + other / 64]
                                & (1u64 << (other % 64))
                                != 0
                            {
                                self.pending.push((next, other));
                            }
                        }
                    }
                }
            }
        }
        let count = self.state.counts[cell];
        if count == 1 {
            self.state.collapsed += 1;
            if let Some(cb) = &self.collapse_callback {
                cb(coordinate(self.size(), cell), self.first(cell) as TileId);
            }
        } else if count == 0 {
            self.state.collapsed -= 1;
            return false;
        } else {
            self.push_heap(cell);
        }
        true
    }
    fn propagate(&mut self) -> bool {
        while let Some((c, t)) = self.pending.pop() {
            if !self.remove(c, t) {
                self.pending.clear();
                return false;
            }
        }
        true
    }
    fn collapse(&mut self, c: usize, chosen: usize) -> bool {
        for t in 0..self.tile_count() {
            if t != chosen && !self.remove(c, t) {
                self.pending.clear();
                return false;
            }
        }
        self.propagate()
    }
    fn undo(&mut self, len: usize) {
        while self.trail.len() > len {
            let (cell, tile) = self.trail.pop().unwrap();
            let t = &self.tables;
            self.state.domains[cell * t.words + tile / 64] |= 1u64 << (tile % 64);
            self.state.counts[cell] += 1;
            self.state.sums[cell] += t.weights[tile];
            self.state.logs[cell] += t.logs[tile];
            for d in 0..6 {
                if let Some(next) = t.neighbors[cell][d] {
                    for &class in &t.affected[tile * 6 + d] {
                        self.state.supports[next * t.classes.len() + class] += 1;
                    }
                }
            }
            match self.state.counts[cell] {
                1 => self.state.collapsed += 1,
                2 => self.state.collapsed -= 1,
                _ => {}
            }
            if self.state.counts[cell] > 1 {
                self.push_heap(cell);
            }
        }
    }
    fn start(&mut self) -> bool {
        if self.initial.is_some() {
            return self.consistent;
        }
        let t = &self.tables;
        for cell in 0..t.cells {
            for (i, class) in t.classes.iter().enumerate() {
                if self.state.supports[cell * t.classes.len() + i] == 0 {
                    for &tile in &class.members {
                        if self.has(cell, tile) {
                            self.pending.push((cell, tile));
                        }
                    }
                }
            }
        }
        self.consistent = !self.failed && self.propagate();
        self.initial = Some(self.state.clone());
        self.begin_attempt(0);
        self.consistent
    }
    fn begin_attempt(&mut self, attempt: u32) {
        self.state = self.initial.as_ref().unwrap().clone();
        self.statistics = Statistics {
            restarts: attempt,
            ..Statistics::default()
        };
        self.decisions.clear();
        self.trail.clear();
        self.pending.clear();
        self.streak = 0;
        self.depth = 0;
        self.failed = !self.consistent;
        self.random = Random::new(
            self.settings
                .seed
                .wrapping_add(u64::from(attempt).wrapping_mul(0x9e3779b97f4a7c15)),
        );
        self.noise = (0..self.cell_count())
            .map(|_| 1e-6 * self.random.unit())
            .collect();
        self.rebuild_heap();
    }
    /// Observe one cell and propagate. `true` means there are more cells to solve.
    pub fn step(&mut self) -> Result<bool, Error> {
        if !self.start() || self.failed {
            return Err(Error::Contradiction);
        }
        if self.finished() {
            return Ok(false);
        }
        let cell = loop {
            if let Some(e) = self.heap.pop() {
                if self.state.counts[e.cell] > 1 && e.key == self.key(e.cell) {
                    break e.cell;
                }
            } else {
                self.rebuild_heap();
            }
        };
        let mut threshold = self.random.unit() * self.state.sums[cell];
        let mut tile = 0;
        for t in 0..self.tile_count() {
            if self.has(cell, t) {
                tile = t;
                threshold -= self.tables.weights[t];
                if threshold < 0. {
                    break;
                }
            }
        }
        if self.settings.max_backtracks > 0 {
            self.decisions.push(Decision {
                cell,
                tile,
                trail: self.trail.len(),
            });
        }
        self.statistics.decisions += 1;
        let mut consistent = self.collapse(cell, tile);
        if consistent && self.decisions.len() > self.depth {
            self.streak = 0;
        }
        while !consistent {
            if self.decisions.is_empty()
                || self.statistics.backtracks >= u64::from(self.settings.max_backtracks)
            {
                self.failed = true;
                return Err(Error::Contradiction);
            }
            self.depth = self.decisions.len();
            let jump = self.decisions.len().min(1usize << self.streak.min(16));
            self.streak += 1;
            let d = self.decisions[self.decisions.len() - jump];
            self.decisions.truncate(self.decisions.len() - jump);
            self.undo(d.trail);
            self.statistics.backtracks += 1;
            if let Some(cb) = &self.reset_callback {
                cb();
            }
            consistent = self.remove(d.cell, d.tile) && self.propagate();
            if !consistent {
                self.pending.clear();
            }
        }
        Ok(!self.finished())
    }
    pub fn restart(&mut self) -> Result<(), Error> {
        if !self.start() {
            return Err(Error::Contradiction);
        }
        let attempt = self
            .statistics
            .restarts
            .checked_add(1)
            .ok_or(Error::Contradiction)?;
        self.begin_attempt(attempt);
        if let Some(cb) = &self.reset_callback {
            cb();
        }
        Ok(())
    }
    fn run_attempt(&mut self) -> bool {
        loop {
            match self.step() {
                Ok(true) => {}
                Ok(false) => return true,
                Err(_) => return false,
            }
        }
    }
    /// Solve, retrying from the original constraints. The earliest successful attempt wins.
    pub fn run(&mut self) -> Result<(), Error> {
        if !self.start() {
            return Err(Error::Contradiction);
        }
        #[cfg(not(target_arch = "wasm32"))]
        let parallel = self.settings.threads != 1
            && self.statistics.decisions == 0
            && self.statistics.restarts == 0
            && self.settings.max_restarts > 0;
        #[cfg(target_arch = "wasm32")]
        let parallel = false;
        if self.run_attempt() {
            return Ok(());
        }
        if parallel {
            #[cfg(not(target_arch = "wasm32"))]
            return self.run_parallel();
        }
        while self.statistics.restarts < self.settings.max_restarts {
            self.restart()?;
            if self.run_attempt() {
                return Ok(());
            }
        }
        Err(Error::Contradiction)
    }
    #[cfg(not(target_arch = "wasm32"))]
    fn run_parallel(&mut self) -> Result<(), Error> {
        use std::sync::atomic::{AtomicU64, Ordering as AtomicOrdering};
        use std::sync::Mutex;
        let workers = if self.settings.threads == 0 {
            std::thread::available_parallelism()
                .map(usize::from)
                .unwrap_or(1)
        } else {
            self.settings.threads
        };
        let next = AtomicU64::new(1);
        let best = AtomicU64::new(u64::from(self.settings.max_restarts) + 1);
        let winner = Mutex::new(None);
        let mut prototype = self.clone();
        prototype.collapse_callback = None;
        prototype.reset_callback = None;
        std::thread::scope(|scope| {
            for _ in 0..workers.min(self.settings.max_restarts as usize) {
                let prototype = &prototype;
                let next = &next;
                let best = &best;
                let winner = &winner;
                scope.spawn(move || loop {
                    let attempt = next.fetch_add(1, AtomicOrdering::Relaxed);
                    if attempt > u64::from(prototype.settings.max_restarts)
                        || attempt > best.load(AtomicOrdering::Relaxed)
                    {
                        break;
                    }
                    let mut s = prototype.clone();
                    s.begin_attempt(attempt as u32);
                    let success = loop {
                        if attempt > best.load(AtomicOrdering::Relaxed) {
                            break false;
                        }
                        match s.step() {
                            Ok(true) => {}
                            Ok(false) => break true,
                            Err(_) => break false,
                        }
                    };
                    if success {
                        let mut guard = winner.lock().unwrap();
                        if attempt < best.load(AtomicOrdering::Relaxed) {
                            best.store(attempt, AtomicOrdering::Relaxed);
                            *guard = Some(s);
                        }
                    }
                });
            }
        });
        if let Some(mut s) = winner.into_inner().unwrap() {
            s.collapse_callback = self.collapse_callback.take();
            s.reset_callback = self.reset_callback.take();
            *self = s;
            Ok(())
        } else {
            self.begin_attempt(self.settings.max_restarts);
            self.failed = true;
            Err(Error::Contradiction)
        }
    }
}
