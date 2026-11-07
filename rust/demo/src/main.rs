mod city;
mod platform;
mod viewer;
use eframe::egui;
use glam::Vec3;
use tinywfc::{Size, Solver};

struct App {
    viewer: viewer::Viewer,
    set: city::Tileset,
    solver: Solver,
    size: Size,
    seed: u64,
    playing: bool,
    speed: f32,
    budget: f32,
    error: String,
    show_open: bool,
    elapsed_ms: f64,
}
impl App {
    fn new(
        cc: &eframe::CreationContext<'_>,
    ) -> Result<Self, Box<dyn std::error::Error + Send + Sync>> {
        platform::theme(&cc.egui_ctx);
        let set = city::tileset();
        let seed = platform::option("seed")
            .and_then(|s| s.parse().ok())
            .unwrap_or(3);
        let size = Size::new(12, 6, 12);
        let solver = city::solver(&set, size, seed)?;
        let mut app = Self {
            viewer: viewer::Viewer::new(cc)?,
            set,
            solver,
            size,
            seed,
            playing: platform::option("paused").as_deref() != Some("1"),
            speed: 120.,
            budget: 0.,
            error: String::new(),
            show_open: true,
            elapsed_ms: 0.,
        };
        app.reset_camera();
        if platform::option("complete").as_deref() == Some("1") {
            let start = platform::now();
            app.solver.run()?;
            app.elapsed_ms = platform::now() - start;
            app.playing = false;
        }
        app.redraw();
        Ok(app)
    }
    fn reset_camera(&mut self) {
        self.viewer.camera = viewer::Camera {
            target: Vec3::new(0., 1., 0.),
            distance: self.size.x.max(self.size.z) as f32 * 1.65,
            yaw: 0.65,
            pitch: 0.6,
        };
    }
    fn regenerate(&mut self) {
        match city::solver(&self.set, self.size, self.seed) {
            Ok(s) => {
                self.solver = s;
                self.error.clear();
                self.budget = 0.;
                self.elapsed_ms = 0.;
                self.playing = true;
                self.reset_camera();
                self.redraw();
            }
            Err(e) => {
                self.error = e.to_string();
                self.playing = false;
            }
        }
    }
    fn step(&mut self) {
        let start = platform::now();
        match self.solver.step() {
            Ok(more) => self.playing = more,
            Err(e) => {
                if self.solver.statistics().restarts < 10 {
                    if let Err(e) = self.solver.restart() {
                        self.error = e.to_string();
                        self.playing = false;
                    }
                } else {
                    self.error = format!("{e}. Try another seed or a smaller grid.");
                    self.playing = false;
                }
            }
        }
        self.elapsed_ms += platform::now() - start;
    }
    fn redraw(&mut self) {
        self.viewer
            .set_geometry(city::geometry(&self.set, &self.solver, self.show_open));
    }
}
impl eframe::App for App {
    fn ui(&mut self, ui: &mut egui::Ui, _: &mut eframe::Frame) {
        let ctx = ui.ctx().clone();
        let mut regenerate = false;
        let mut redraw = false;
        let mut single_step = false;
        platform::controls(ui, |ui| {
            platform::heading(ui, "tinywfc", "Watch socket rules assemble a 3D city");
            ui.label("GENERATOR");
            ui.horizontal(|ui| {
                ui.label("Seed");
                ui.add(egui::DragValue::new(&mut self.seed));
            });
            ui.add(egui::Slider::new(&mut self.size.x, 6..=20).text("Width"));
            ui.add(egui::Slider::new(&mut self.size.z, 6..=20).text("Depth"));
            ui.add(egui::Slider::new(&mut self.size.y, 3..=8).text("Layers"));
            ui.horizontal(|ui| {
                if ui.button("Generate").clicked() {
                    regenerate = true;
                }
                if ui.button("New seed").clicked() {
                    self.seed = self.seed.wrapping_add(1);
                    regenerate = true;
                }
            });
            ui.separator();
            ui.label("PLAYBACK");
            ui.horizontal(|ui| {
                if ui
                    .button(if self.playing { "Pause" } else { "Play" })
                    .clicked()
                {
                    self.playing = !self.playing && !self.solver.finished();
                }
                single_step = ui.button("One step").clicked();
            });
            ui.add(
                egui::Slider::new(&mut self.speed, 1.0..=1500.)
                    .logarithmic(true)
                    .text("Steps / s"),
            );
            redraw |= ui
                .checkbox(&mut self.show_open, "Show unresolved ground")
                .changed();
            ui.checkbox(&mut self.viewer.orbit, "Auto orbit");
            if ui.button("Reset camera").clicked() {
                self.reset_camera();
            }
            ui.separator();
            let fraction = self.solver.collapsed_count() as f32 / self.solver.cell_count() as f32;
            ui.add(egui::ProgressBar::new(fraction).show_percentage());
            ui.label(if self.solver.finished() {
                "City complete"
            } else if self.playing {
                "Generating..."
            } else {
                "Paused"
            });
            ui.label(format!(
                "{} / {} cells",
                self.solver.collapsed_count(),
                self.solver.cell_count()
            ));
            let stats = self.solver.statistics();
            ui.small(format!(
                "{} decisions  |  {} backtracks\nAttempt {}  |  {:.1} ms of solving",
                stats.decisions,
                stats.backtracks,
                stats.restarts + 1,
                self.elapsed_ms
            ));
            if !self.error.is_empty() {
                ui.colored_label(egui::Color32::LIGHT_RED, &self.error);
            }
            ui.separator();
            ui.small("Roads, canals and parks form the ground. Vertical sockets stack buildings, roofs, ramps and elevated roads.");
            ui.small("Drag: orbit  |  Right drag: pan\nScroll: zoom");
            ui.hyperlink_to(
                "Library & source",
                "https://github.com/varnholt/tinywfc/tree/main/rust",
            );
        });
        if regenerate {
            self.regenerate();
        }
        if single_step && !self.solver.finished() {
            self.step();
            self.playing = false;
            redraw = true;
        }
        if self.playing {
            self.budget = (self.budget + ctx.input(|i| i.stable_dt.min(0.1)) * self.speed).min(30.);
            let start = platform::now();
            while self.budget >= 1. && self.playing && platform::now() - start < 6. {
                self.step();
                self.budget -= 1.;
                redraw = true;
            }
            ctx.request_repaint();
        }
        if redraw {
            self.redraw();
        }
        egui::CentralPanel::default().show(ui, |ui| {
            self.viewer.show(ui);
        });
    }
    fn on_exit(&mut self, gl: Option<&eframe::glow::Context>) {
        if let Some(gl) = gl {
            self.viewer.destroy(gl);
        }
    }
}
#[cfg(not(target_arch = "wasm32"))]
fn main() -> eframe::Result {
    platform::run(
        "tinywfc  |  Rust demo",
        Box::new(|cc| Ok(Box::new(App::new(cc)?))),
    )
}
#[cfg(target_arch = "wasm32")]
fn main() {
    platform::run("tinywfc", Box::new(|cc| Ok(Box::new(App::new(cc)?))));
}
