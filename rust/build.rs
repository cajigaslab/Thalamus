//use bindgen;
//use std::env;
//use std::path::PathBuf;
//use std::process::Command;

fn main() {    
  //let output = Command::new("python")
  //  .args(["-m", "thalamus.cflags"])
  //  .output()
  //  .expect("Failed to find plugin.h");
  //let stdout_raw = String::from_utf8(output.stdout).expect("Failed to parse stdout");
  //let stdout = stdout_raw.trim();
  //println!("cflags {}", stdout);
  //let dir = stdout.strip_prefix("-I").expect("Strip prefix failed");
  //let path = dir.to_owned() + "/thalamus/plugin.h";
  //println!("path {}", path);
//
//
  //let bindings = bindgen::Builder::default()
  //  .header(path)
  //  .generate_cstr(true)
  //  .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
  //  .generate()
  //  .expect("Unable to generate bindings");
//
  //// Write the bindings to the $OUT_DIR/bindings.rs file.
  //let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
  //bindings
  //    .write_to_file(out_path.join("bindings.rs"))
  //    .expect("Couldn't write bindings!");
}