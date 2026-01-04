use serde::{Deserialize, Serialize};
use std::io::Write;

#[derive(Serialize, Deserialize)]
struct Ply {
    pos: [f32; 3],
    normal: [f32; 3],
    f_dc: [f32; 3],
    opacity: f32,
    scale: [f32; 3],
    rot: [f32; 4],
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let path = "sample.ply";
    let mut file = std::fs::File::create(path)?;
    file.write(b"ply\n")?;
    file.write(b"format binary_little_endian 1.0\n")?;
    write!(file, "element vertex {}\n", 4 * 4 * 4 * 4)?;
    file.write(
        r#"property float x
property float y
property float z
property float nx
property float ny
property float nz
property float f_dc_0
property float f_dc_1
property float f_dc_2
property float opacity
property float scale_0
property float scale_1
property float scale_2
property float rot_0
property float rot_1
property float rot_2
property float rot_3
"#
        .as_bytes(),
    )?;
    file.write(b"end_header\n")?;

    for x in 0..4 {
        for y in 0..4 {
            for z in 0..4 {
                let base_pos = [x as f32 * 20.0, y as f32 * 20.0, z as f32 * 20.0];
                file.write(&postcard::to_vec::<Ply, 68>(&Ply {
                    pos: [base_pos[0], base_pos[1], base_pos[2]],
                    normal: [0.0, 0.0, 0.0],
                    f_dc: [1.77, 0.0, 1.77],
                    opacity: 10.0,
                    scale: [0.1, 0.1, 0.1],
                    rot: [1.0, 0.0, 0.0, 0.0],
                })?)?;
                file.write(&postcard::to_vec::<Ply, 68>(&Ply {
                    pos: [base_pos[0] + 7.0, base_pos[1], base_pos[2]],
                    normal: [0.0, 0.0, 0.0],
                    f_dc: [1.77, 0.0, 0.0],
                    opacity: 10.0,
                    scale: [1.0, 0.1, 0.1],
                    rot: [1.0, 0.0, 0.0, 0.0],
                })?)?;
                file.write(&postcard::to_vec::<Ply, 68>(&Ply {
                    pos: [base_pos[0], base_pos[1] + 7.0, base_pos[2]],
                    normal: [0.0, 0.0, 0.0],
                    f_dc: [0.0, 1.77, 0.0],
                    opacity: 10.0,
                    scale: [0.1, 1.0, 0.1],
                    rot: [1.0, 0.0, 0.0, 0.0],
                })?)?;
                file.write(&postcard::to_vec::<Ply, 68>(&Ply {
                    pos: [base_pos[0], base_pos[1], base_pos[2] + 7.0],
                    normal: [0.0, 0.0, 0.0],
                    f_dc: [0.0, 0.0, 1.77],
                    opacity: 10.0,
                    scale: [0.1, 0.1, 1.0],
                    rot: [1.0, 0.0, 0.0, 0.0],
                })?)?;
            }
        }
    }

    Ok(())
}
