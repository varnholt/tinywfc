use tinywfc::*;
#[test]
fn seeded_results_and_search_statistics_match_cpp() {
    let mut successes = 0;
    let mut backtracks = 0;
    for line in include_str!("data/cpp_reference.txt").lines() {
        let values: Vec<u64> = line
            .split_whitespace()
            .map(|v| v.parse().unwrap())
            .collect();
        let seed = values[0];
        let mut rules = Rules::new(4).unwrap();
        let mut state = seed + 1;
        for d in [Direction::PositiveX, Direction::PositiveZ] {
            for a in 0..4 {
                for b in 0..4 {
                    state = state.wrapping_mul(6364136223846793005).wrapping_add(1);
                    if state >> 62 != 0 {
                        rules.allow(a, d, b).unwrap();
                    }
                }
            }
        }
        let mut solver = Solver::new(
            &rules,
            Settings {
                size: Size::new(4, 1, 4),
                seed,
                max_backtracks: 100,
                max_restarts: 3,
                ..Settings::default()
            },
        )
        .unwrap();
        let result = solver.run();
        assert_eq!(result.is_ok(), values[1] != 0, "seed {seed}");
        if result.is_ok() {
            successes += 1;
            let stats = solver.statistics();
            assert_eq!(
                [stats.decisions, stats.backtracks, u64::from(stats.restarts)],
                [values[2], values[3], values[4]],
                "seed {seed}"
            );
            backtracks += stats.backtracks;
            assert_eq!(
                solver
                    .result()
                    .unwrap()
                    .iter()
                    .map(|&v| u64::from(v))
                    .collect::<Vec<_>>(),
                values[5..],
                "seed {seed}"
            );
        }
    }
    assert!(successes > 0);
    assert!(backtracks > 0);
}
