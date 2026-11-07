use tinywfc::{Coordinate, Rules, Settings, Size, Solver};
fn main() -> Result<(), tinywfc::Error> {
    let mut rules = Rules::new(2)?;
    rules.allow_everywhere(0, 1)?;
    let mut solver = Solver::new(
        &rules,
        Settings {
            size: Size::new(16, 1, 8),
            seed: 42,
            ..Settings::default()
        },
    )?;
    solver.run()?;
    for z in 0..8 {
        for x in 0..16 {
            print!(
                "{}",
                if solver.tile(Coordinate::new(x, 0, z)) == Some(0) {
                    '.'
                } else {
                    '#'
                }
            );
        }
        println!();
    }
    Ok(())
}
