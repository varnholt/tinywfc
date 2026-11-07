use eframe::egui;

pub fn theme(ctx: &egui::Context) {
    ctx.set_theme(egui::ThemePreference::Dark);
    let mut visuals = egui::Visuals::dark();
    visuals.panel_fill = egui::Color32::from_rgb(20, 29, 42);
    visuals.window_fill = visuals.panel_fill;
    visuals.extreme_bg_color = egui::Color32::from_rgb(12, 18, 28);
    visuals.selection.bg_fill = egui::Color32::from_rgb(30, 106, 104);
    visuals.hyperlink_color = egui::Color32::from_rgb(102, 219, 198);
    ctx.set_visuals(visuals);
    ctx.style_mut_of(egui::Theme::Dark, |s| {
        s.spacing.item_spacing = egui::vec2(10., 10.);
        s.spacing.slider_width = 135.;
    });
}

pub fn heading(ui: &mut egui::Ui, name: &str, subtitle: &str) {
    ui.add_space(12.);
    ui.label(
        egui::RichText::new("RUST / INTERACTIVE DEMO")
            .size(10.)
            .color(egui::Color32::from_rgb(102, 219, 198)),
    );
    ui.heading(name);
    ui.label(
        egui::RichText::new(subtitle)
            .small()
            .color(egui::Color32::from_gray(170)),
    );
    ui.add_space(4.);
    ui.separator();
}

pub fn option(key: &str) -> Option<String> {
    #[cfg(not(target_arch = "wasm32"))]
    {
        let args: Vec<_> = std::env::args().collect();
        let flag = format!("--{key}");
        for (i, arg) in args.iter().enumerate() {
            if arg == &flag {
                return args.get(i + 1).cloned();
            }
            if let Some(value) = arg.strip_prefix(&format!("{flag}=")) {
                return Some(value.to_owned());
            }
        }
        None
    }
    #[cfg(target_arch = "wasm32")]
    {
        let search = web_sys::window()?.location().search().ok()?;
        web_sys::UrlSearchParams::new_with_str(&search)
            .ok()?
            .get(key)
    }
}

#[cfg(not(target_arch = "wasm32"))]
pub fn run(title: &str, creator: eframe::AppCreator<'static>) -> eframe::Result {
    eframe::run_native(
        title,
        eframe::NativeOptions {
            viewport: egui::ViewportBuilder::default()
                .with_inner_size([1180., 780.])
                .with_min_inner_size([640., 440.]),
            renderer: eframe::Renderer::Glow,
            depth_buffer: 24,
            multisampling: 4,
            ..Default::default()
        },
        creator,
    )
}

#[cfg(target_arch = "wasm32")]
pub fn run(_title: &str, creator: eframe::AppCreator<'static>) {
    use wasm_bindgen::JsCast;
    wasm_bindgen_futures::spawn_local(async move {
        let document = web_sys::window().unwrap().document().unwrap();
        let canvas = document
            .get_element_by_id("canvas")
            .unwrap()
            .dyn_into::<web_sys::HtmlCanvasElement>()
            .unwrap();
        let options = eframe::WebOptions {
            depth_buffer: 24,
            ..Default::default()
        };
        match eframe::WebRunner::new()
            .start(canvas, options, creator)
            .await
        {
            Ok(()) => {
                if let Some(canvas) = document.get_element_by_id("canvas") {
                    let _ = canvas.set_attribute("data-ready", "true");
                }
                if let Some(loading) = document.get_element_by_id("loading") {
                    loading.remove();
                }
            }
            Err(error) => {
                if let Some(loading) = document.get_element_by_id("loading") {
                    loading.set_text_content(Some(&format!("Could not start the demo: {error:?}")));
                }
            }
        }
    });
}

/// Monotonic milliseconds on native and browser targets.
pub fn now() -> f64 {
    #[cfg(not(target_arch = "wasm32"))]
    {
        static START: std::sync::OnceLock<std::time::Instant> = std::sync::OnceLock::new();
        START
            .get_or_init(std::time::Instant::now)
            .elapsed()
            .as_secs_f64()
            * 1000.
    }
    #[cfg(target_arch = "wasm32")]
    {
        web_sys::window().unwrap().performance().unwrap().now()
    }
}

pub fn controls(ui: &mut egui::Ui, contents: impl FnOnce(&mut egui::Ui)) {
    egui::Panel::left("controls")
        .exact_size(265.)
        .show(ui, |ui| {
            egui::ScrollArea::vertical()
                .auto_shrink([false, false])
                .show(ui, contents);
        });
}
