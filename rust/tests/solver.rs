use tinywfc::*;

#[test]
fn domains_cross_word_boundaries_and_keep_weights() {
    let tiles: Vec<_> = (0..130)
        .map(|_| SocketTile {
            sockets: [0; 6],
            weight: 1.,
        })
        .collect();
    let mut rules = rules_from_sockets(&tiles).unwrap();
    rules.set_weight(129, 3.).unwrap();
    assert_eq!(rules.weight(129), Some(3.));
    let mut solver = Solver::new(
        &rules,
        Settings {
            size: Size::new(4, 1, 1),
            ..Settings::default()
        },
    )
    .unwrap();
    for (x, tile) in [63, 64, 127, 129].into_iter().enumerate() {
        solver
            .restrict(Coordinate::new(x as i32, 0, 0), &[tile])
            .unwrap();
    }
    solver.run().unwrap();
    assert_eq!(solver.result(), Some(vec![63, 64, 127, 129]));
    assert_eq!(rules.set_weight(0, f32::INFINITY), Err(Error::InvalidRules));
    assert_eq!(rules.set_weight(0, 0.), Err(Error::InvalidRules));
}
fn colors(n: usize) -> Rules {
    let mut r = Rules::new(n).unwrap();
    for a in 0..n {
        for b in a + 1..n {
            r.allow_everywhere(a as u32, b as u32).unwrap();
        }
    }
    r
}
fn check(rules: &Rules, s: &Solver) {
    assert!(s.finished());
    assert_eq!(s.result().unwrap().len(), s.cell_count());
    for i in 0..s.cell_count() {
        let c = s.coordinate(i).unwrap();
        assert_eq!(s.cell_index(c), Some(i));
        let a = s.tile(c).unwrap();
        for d in DIRECTIONS {
            let mut next = adjacent(c, d);
            let size = s.size();
            let axis = match d {
                Direction::PositiveX | Direction::NegativeX => size.x,
                Direction::PositiveY | Direction::NegativeY => size.y,
                _ => size.z,
            };
            if !s.contains(next) && s.settings().periodic && axis > 1 {
                next.x = next.x.rem_euclid(size.x);
                next.y = next.y.rem_euclid(size.y);
                next.z = next.z.rem_euclid(size.z);
            }
            if let Some(b) = s.tile(next) {
                assert!(rules.allows(a, d, b), "{c:?}, {d:?}: {a} -> {b}");
            }
        }
    }
}
#[test]
fn solve_2d_and_3d_periodic() {
    let r = colors(2);
    for size in [Size::new(12, 1, 8), Size::new(6, 4, 8)] {
        for seed in 0..8 {
            let mut s = Solver::new(
                &r,
                Settings {
                    size,
                    seed,
                    periodic: true,
                    ..Settings::default()
                },
            )
            .unwrap();
            s.run().unwrap();
            check(&r, &s);
        }
    }
}
#[test]
fn constraints_survive_restart_and_clone_is_independent() {
    let r = colors(3);
    let c = Coordinate::new(0, 0, 0);
    let mut s = Solver::new(&r, Settings::default()).unwrap();
    s.restrict(c, &[2]).unwrap();
    let mut copy = s.clone();
    s.run().unwrap();
    copy.restart().unwrap();
    assert_eq!(copy.tile(c), Some(2));
    copy.run().unwrap();
    check(&r, &s);
    check(&r, &copy);
}
#[test]
fn impossible_odd_ring_stays_failed() {
    let r = colors(2);
    let mut s = Solver::new(
        &r,
        Settings {
            size: Size::new(3, 1, 1),
            periodic: true,
            max_restarts: 2,
            ..Settings::default()
        },
    )
    .unwrap();
    assert_eq!(s.run(), Err(Error::Contradiction));
    assert_eq!(s.step(), Err(Error::Contradiction));
    assert!(!s.finished());
}
#[test]
fn missing_support_is_detected_even_with_one_tile() {
    let r = Rules::new(1).unwrap();
    let mut s = Solver::new(&r, Settings::default()).unwrap();
    assert_eq!(s.run(), Err(Error::Contradiction));
    let mut flat = Solver::new(
        &r,
        Settings {
            size: Size::new(1, 1, 1),
            periodic: true,
            ..Settings::default()
        },
    )
    .unwrap();
    flat.run().unwrap();
    assert_eq!(flat.result(), Some(vec![0]));
}
#[test]
fn sockets_rotation_and_error_validation() {
    assert_eq!(rotate_y([0, 1, 2, 3, 4, 5], 1), [4, 5, 2, 3, 1, 0]);
    assert_eq!(
        rotate_y(rotate_y([0, 1, 2, 3, 4, 5], 1), -1),
        [0, 1, 2, 3, 4, 5]
    );
    let r = rules_from_sockets(&[
        SocketTile {
            sockets: [1; 6],
            weight: 2.,
        },
        SocketTile {
            sockets: [1; 6],
            weight: 1.,
        },
    ])
    .unwrap();
    assert!(r.allows(0, Direction::PositiveY, 1));
    assert!(matches!(Rules::new(0), Err(Error::InvalidRules)));
    assert!(matches!(
        Solver::new(
            &r,
            Settings {
                size: Size::new(i32::MAX, i32::MAX, i32::MAX),
                ..Settings::default()
            }
        ),
        Err(Error::InvalidSize)
    ));
    let mut s = Solver::new(&r, Settings::default()).unwrap();
    assert_eq!(
        s.ban(Coordinate::new(-1, 0, 0), 0),
        Err(Error::InvalidCoordinate)
    );
    assert_eq!(
        s.restrict(Coordinate::default(), &[99]),
        Err(Error::InvalidTile)
    );
    assert_eq!(
        s.set_priority(Coordinate::default(), f64::NAN),
        Err(Error::InvalidPriority)
    );
    assert_eq!(
        s.restrict(Coordinate::default(), &[]),
        Err(Error::Contradiction)
    );
    assert_eq!(s.run(), Err(Error::Contradiction));
}
#[test]
fn priorities_and_callbacks() {
    use std::sync::{Arc, Mutex};
    let mut r = Rules::new(2).unwrap();
    for a in 0..2 {
        for b in 0..2 {
            r.allow_everywhere(a, b).unwrap();
        }
    }
    let mut s = Solver::new(
        &r,
        Settings {
            size: Size::new(3, 1, 1),
            ..Settings::default()
        },
    )
    .unwrap();
    s.set_priority(Coordinate::new(2, 0, 0), -100.).unwrap();
    let events = Arc::new(Mutex::new(Vec::new()));
    let e = events.clone();
    s.set_collapse_callback(move |c, t| e.lock().unwrap().push((c, t)));
    s.step().unwrap();
    assert_eq!(events.lock().unwrap()[0].0, Coordinate::new(2, 0, 0));
    s.run().unwrap();
    assert_eq!(events.lock().unwrap().len(), 3);
}
#[test]
fn parallel_restarts_match_serial_including_winning_attempt() {
    // Random sparse reciprocal rules exercise successful retries as well as unsatisfiable grids.
    let mut retries = 0;
    for case in 0..100u64 {
        let mut r = Rules::new(4).unwrap();
        let mut state = case + 1;
        for d in [Direction::PositiveX, Direction::PositiveZ] {
            for a in 0..4 {
                for b in 0..4 {
                    state = state.wrapping_mul(6364136223846793005).wrapping_add(1);
                    if state >> 62 != 0 {
                        r.allow(a, d, b).unwrap();
                    }
                }
            }
        }
        let settings = Settings {
            size: Size::new(4, 1, 4),
            seed: case,
            max_backtracks: 0,
            max_restarts: 8,
            ..Settings::default()
        };
        let mut serial = Solver::new(&r, settings.clone()).unwrap();
        let mut parallel = Solver::new(
            &r,
            Settings {
                threads: 4,
                ..settings
            },
        )
        .unwrap();
        let a = serial.run();
        let b = parallel.run();
        assert_eq!(a, b, "case {case}");
        if a.is_ok() {
            check(&r, &parallel);
            assert_eq!(serial.result(), parallel.result());
            assert_eq!(serial.statistics(), parallel.statistics());
            if serial.statistics().restarts > 0 {
                retries += 1;
            }
        }
    }
    assert!(
        retries > 0,
        "fixture must exercise a successful parallel retry"
    );
}
#[test]
fn backtracking_recovers_contradictions() {
    let mut recovered = 0;
    for case in 0..100u64 {
        let mut r = Rules::new(4).unwrap();
        let mut state = case + 1;
        for d in [Direction::PositiveX, Direction::PositiveZ] {
            for a in 0..4 {
                for b in 0..4 {
                    state = state.wrapping_mul(6364136223846793005).wrapping_add(1);
                    if state >> 62 != 0 {
                        r.allow(a, d, b).unwrap();
                    }
                }
            }
        }
        let mut s = Solver::new(
            &r,
            Settings {
                size: Size::new(4, 1, 4),
                seed: case,
                max_restarts: 0,
                ..Settings::default()
            },
        )
        .unwrap();
        if s.run().is_ok() {
            check(&r, &s);
            if s.statistics().backtracks > 0 {
                recovered += 1;
            }
        }
    }
    assert!(recovered > 0, "fixtures must exercise successful undo");
}
