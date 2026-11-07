//! Small OpenGL/WebGL2 viewer, owned by the demo rather than the library crate.
use eframe::{egui, glow};
use glam::{Mat4, Vec3};
use glow::HasContext;
use std::sync::{Arc, Mutex};

#[repr(C)]
#[derive(Clone, Copy, bytemuck::Pod, bytemuck::Zeroable)]
pub struct Vertex {
    position: [f32; 3],
    normal: [f32; 3],
    color: [f32; 3],
}
#[derive(Default, Clone)]
pub struct Geometry {
    triangles: Vec<Vertex>,
    lines: Vec<Vertex>,
    points: Vec<Vertex>,
}
#[allow(dead_code)] // Shared viewer helpers used by different scenes.
impl Geometry {
    pub fn triangle(&mut self, p: [Vec3; 3], color: [f32; 3]) {
        let n = (p[1] - p[0]).cross(p[2] - p[0]).normalize_or_zero();
        self.smooth_triangle(p, [n; 3], color);
    }
    pub fn smooth_triangle(&mut self, p: [Vec3; 3], normals: [Vec3; 3], color: [f32; 3]) {
        for i in 0..3 {
            self.triangles.push(Vertex {
                position: p[i].to_array(),
                normal: normals[i].to_array(),
                color,
            });
        }
    }
    pub fn line(&mut self, a: Vec3, b: Vec3, color: [f32; 3]) {
        for p in [a, b] {
            self.lines.push(Vertex {
                position: p.to_array(),
                normal: [0.; 3],
                color,
            });
        }
    }
    pub fn point(&mut self, p: Vec3, color: [f32; 3]) {
        self.points.push(Vertex {
            position: p.to_array(),
            normal: [0.; 3],
            color,
        });
    }
    pub fn quad(&mut self, p: [Vec3; 4], color: [f32; 3]) {
        self.triangle([p[0], p[1], p[2]], color);
        self.triangle([p[0], p[2], p[3]], color);
    }
    pub fn cuboid(&mut self, min: Vec3, max: Vec3, color: [f32; 3]) {
        let p: [Vec3; 8] = std::array::from_fn(|i| {
            Vec3::new(
                if i & 1 == 0 { min.x } else { max.x },
                if i & 2 == 0 { min.y } else { max.y },
                if i & 4 == 0 { min.z } else { max.z },
            )
        });
        for [a, b, c, d] in [
            [0, 2, 3, 1],
            [4, 5, 7, 6],
            [0, 4, 6, 2],
            [1, 3, 7, 5],
            [0, 1, 5, 4],
            [2, 6, 7, 3],
        ] {
            self.quad([p[a], p[b], p[c], p[d]], color);
        }
    }
    pub fn sphere(&mut self, center: Vec3, radius: f32, color: [f32; 3]) {
        let at = |r: u32, s: u32| {
            let phi = std::f32::consts::PI * r as f32 / 16.;
            let theta = std::f32::consts::TAU * s as f32 / 24.;
            Vec3::new(phi.sin() * theta.cos(), phi.cos(), phi.sin() * theta.sin())
        };
        for r in 0..16 {
            for s in 0..24 {
                let normals = [at(r, s), at(r + 1, s), at(r + 1, s + 1), at(r, s + 1)];
                for [a, b, c] in [[0, 1, 2], [0, 2, 3]] {
                    self.smooth_triangle(
                        [
                            center + normals[a] * radius,
                            center + normals[b] * radius,
                            center + normals[c] * radius,
                        ],
                        [normals[a], normals[b], normals[c]],
                        color,
                    );
                }
            }
        }
    }
    pub fn grid(&mut self, half: i32, y: f32, spacing: f32) {
        let h = half as f32 * spacing;
        for i in -half..=half {
            let p = i as f32 * spacing;
            let color = if i == 0 {
                [0.24, 0.35, 0.43]
            } else {
                [0.13, 0.20, 0.27]
            };
            self.line(Vec3::new(-h, y, p), Vec3::new(h, y, p), color);
            self.line(Vec3::new(p, y, -h), Vec3::new(p, y, h), color);
        }
    }
    pub fn triangle_count(&self) -> usize {
        self.triangles.len() / 3
    }
}

#[derive(Clone)]
pub struct Camera {
    pub target: Vec3,
    pub distance: f32,
    pub yaw: f32,
    pub pitch: f32,
}
impl Default for Camera {
    fn default() -> Self {
        Self {
            target: Vec3::ZERO,
            distance: 5.5,
            yaw: 0.65,
            pitch: 0.45,
        }
    }
}
#[allow(dead_code)] // Picking is only needed by the particle demo.
impl Camera {
    pub fn eye(&self) -> Vec3 {
        self.target
            + self.distance
                * Vec3::new(
                    self.pitch.cos() * self.yaw.sin(),
                    self.pitch.sin(),
                    self.pitch.cos() * self.yaw.cos(),
                )
    }
    fn matrix(&self, aspect: f32) -> Mat4 {
        Mat4::perspective_rh_gl(
            45_f32.to_radians(),
            aspect.max(0.01),
            0.01,
            (self.distance * 20.).max(100.),
        ) * Mat4::look_at_rh(self.eye(), self.target, Vec3::Y)
    }
    pub fn project(&self, p: Vec3, rect: egui::Rect) -> Option<egui::Pos2> {
        let clip = self.matrix(rect.aspect_ratio()) * p.extend(1.);
        if clip.w <= 0. {
            return None;
        }
        let n = clip.truncate() / clip.w;
        Some(egui::pos2(
            rect.center().x + n.x * rect.width() * 0.5,
            rect.center().y - n.y * rect.height() * 0.5,
        ))
    }
    pub fn drag_point(&self, pos: egui::Pos2, rect: egui::Rect, point: Vec3) -> Vec3 {
        let n = egui::vec2(
            (pos.x - rect.center().x) / rect.width() * 2.,
            -(pos.y - rect.center().y) / rect.height() * 2.,
        );
        let inv = self.matrix(rect.aspect_ratio()).inverse();
        let p = inv * glam::Vec4::new(n.x, n.y, 1., 1.);
        let direction = (p.truncate() / p.w - self.eye()).normalize_or_zero();
        let normal = (self.target - self.eye()).normalize();
        let denominator = direction.dot(normal);
        if denominator.abs() < 1e-5 {
            point
        } else {
            self.eye() + direction * ((point - self.eye()).dot(normal) / denominator)
        }
    }
}

pub struct Viewer {
    pub camera: Camera,
    pub orbit: bool,
    pub geometry: Arc<Geometry>,
    renderer: Arc<Mutex<Renderer>>,
}
impl Viewer {
    pub fn new(
        cc: &eframe::CreationContext<'_>,
    ) -> Result<Self, Box<dyn std::error::Error + Send + Sync>> {
        let gl = cc.gl.as_ref().ok_or("OpenGL is unavailable")?;
        Ok(Self {
            camera: Camera::default(),
            orbit: false,
            geometry: Arc::new(Geometry::default()),
            renderer: Arc::new(Mutex::new(unsafe { Renderer::new(gl)? })),
        })
    }
    pub fn set_geometry(&mut self, geometry: Geometry) {
        self.geometry = Arc::new(geometry);
    }
    pub fn show(&mut self, ui: &mut egui::Ui) -> egui::Response {
        let (rect, response) = ui.allocate_exact_size(
            ui.available_size().max(egui::vec2(10., 10.)),
            egui::Sense::click_and_drag(),
        );
        let (delta, shift, scroll, dt) = ui.input(|i| {
            (
                i.pointer.delta(),
                i.modifiers.shift,
                i.smooth_scroll_delta.y,
                i.stable_dt.min(0.05),
            )
        });
        if response.dragged_by(egui::PointerButton::Primary) && !shift {
            self.camera.yaw -= delta.x * 0.008;
            self.camera.pitch = (self.camera.pitch + delta.y * 0.008).clamp(-1.45, 1.45);
        }
        if response.dragged_by(egui::PointerButton::Secondary) {
            let forward = (self.camera.target - self.camera.eye()).normalize();
            let right = forward.cross(Vec3::Y).normalize();
            let up = right.cross(forward);
            self.camera.target += (-right * delta.x + up * delta.y) * self.camera.distance * 0.0015;
        }
        if response.hovered() && scroll != 0. {
            self.camera.distance =
                (self.camera.distance * (-scroll * 0.002).exp()).clamp(0.3, 150.);
        }
        if self.orbit {
            self.camera.yaw += dt * 0.22;
            ui.ctx().request_repaint();
        }
        let camera = self.camera.clone();
        let geometry = self.geometry.clone();
        let renderer = self.renderer.clone();
        ui.painter().add(egui::PaintCallback {
            rect,
            callback: Arc::new(eframe::egui_glow::CallbackFn::new(move |info, painter| {
                let mut renderer = renderer.lock().unwrap();
                // SAFETY: eframe invokes this on the render thread with its OpenGL context current.
                unsafe {
                    renderer.paint(
                        painter.gl(),
                        &geometry,
                        camera.matrix(info.viewport.aspect_ratio()),
                    );
                }
            })),
        });
        response
    }
    pub fn destroy(&mut self, gl: &glow::Context) {
        unsafe {
            self.renderer.lock().unwrap().destroy(gl);
        }
    }
}

struct Renderer {
    program: glow::Program,
    vao: glow::VertexArray,
    buffer: glow::Buffer,
    live: bool,
}
impl Renderer {
    unsafe fn new(gl: &glow::Context) -> Result<Self, String> {
        let prefix = if cfg!(target_arch = "wasm32") {
            "#version 300 es\nprecision highp float;\n"
        } else {
            "#version 330 core\n"
        };
        let program = gl.create_program()?;
        let vertex=format!("{prefix}layout(location=0) in vec3 a_pos; layout(location=1) in vec3 a_normal; layout(location=2) in vec3 a_color; uniform mat4 u_vp; out vec3 v_normal; out vec3 v_color; void main() {{gl_Position=u_vp*vec4(a_pos,1.);gl_PointSize=5.;v_normal=a_normal;v_color=a_color;}}");
        let fragment=format!("{prefix}in vec3 v_normal; in vec3 v_color; uniform float u_lit; out vec4 out_color; void main() {{float light=0.38+0.62*abs(dot(normalize(v_normal+vec3(0.000001)),normalize(vec3(0.4,0.9,0.6))));out_color=vec4(v_color*mix(1.,light,u_lit),1.);}}");
        for (kind, source) in [
            (glow::VERTEX_SHADER, vertex),
            (glow::FRAGMENT_SHADER, fragment),
        ] {
            let shader = gl.create_shader(kind)?;
            gl.shader_source(shader, &source);
            gl.compile_shader(shader);
            if !gl.get_shader_compile_status(shader) {
                let error = gl.get_shader_info_log(shader);
                gl.delete_shader(shader);
                gl.delete_program(program);
                return Err(error);
            }
            gl.attach_shader(program, shader);
            gl.delete_shader(shader);
        }
        gl.link_program(program);
        if !gl.get_program_link_status(program) {
            let error = gl.get_program_info_log(program);
            gl.delete_program(program);
            return Err(error);
        }
        let vao = gl.create_vertex_array()?;
        let buffer = gl.create_buffer()?;
        gl.bind_vertex_array(Some(vao));
        gl.bind_buffer(glow::ARRAY_BUFFER, Some(buffer));
        for i in 0..3 {
            gl.enable_vertex_attrib_array(i);
            gl.vertex_attrib_pointer_f32(i, 3, glow::FLOAT, false, 36, i as i32 * 12);
        }
        gl.bind_vertex_array(None);
        Ok(Self {
            program,
            vao,
            buffer,
            live: true,
        })
    }
    unsafe fn paint(&mut self, gl: &glow::Context, g: &Geometry, vp: Mat4) {
        if !self.live {
            return;
        }
        gl.clear_color(0.045, 0.065, 0.095, 1.);
        gl.clear_depth_f32(1.);
        gl.depth_mask(true);
        gl.clear(glow::COLOR_BUFFER_BIT | glow::DEPTH_BUFFER_BIT);
        gl.enable(glow::DEPTH_TEST);
        gl.depth_func(glow::LEQUAL);
        gl.disable(glow::CULL_FACE);
        gl.disable(glow::BLEND);
        #[cfg(not(target_arch = "wasm32"))]
        gl.enable(glow::PROGRAM_POINT_SIZE);
        gl.use_program(Some(self.program));
        gl.uniform_matrix_4_f32_slice(
            gl.get_uniform_location(self.program, "u_vp").as_ref(),
            false,
            &vp.to_cols_array(),
        );
        gl.bind_vertex_array(Some(self.vao));
        gl.bind_buffer(glow::ARRAY_BUFFER, Some(self.buffer));
        for (vertices, mode, lit) in [
            (&g.triangles, glow::TRIANGLES, 1.),
            (&g.lines, glow::LINES, 0.),
            (&g.points, glow::POINTS, 0.),
        ] {
            gl.uniform_1_f32(gl.get_uniform_location(self.program, "u_lit").as_ref(), lit);
            gl.buffer_data_u8_slice(
                glow::ARRAY_BUFFER,
                bytemuck::cast_slice(vertices),
                glow::STREAM_DRAW,
            );
            gl.draw_arrays(mode, 0, vertices.len() as i32);
        }
        gl.bind_vertex_array(None);
        gl.use_program(None);
        gl.disable(glow::DEPTH_TEST);
    }
    unsafe fn destroy(&mut self, gl: &glow::Context) {
        if self.live {
            gl.delete_program(self.program);
            gl.delete_buffer(self.buffer);
            gl.delete_vertex_array(self.vao);
            self.live = false;
        }
    }
}
