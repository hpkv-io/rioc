{
  "targets": [{
    "target_name": "rioc",
    "cflags!": [ "-fno-exceptions" ],
    "cflags_cc!": [ "-fno-exceptions" ],
    "cflags": [ "-O3" ],
    "cflags_cc": [ "-O3" ],
    "xcode_settings": {
      "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
      "CLANG_CXX_LIBRARY": "libc++",
      "MACOSX_DEPLOYMENT_TARGET": "10.15",
      "OTHER_CFLAGS": [ "-O3" ]
    },
    "msvs_settings": {
      "VCCLCompilerTool": { 
        "ExceptionHandling": 1,
        "Optimization": 2
      }
    },
    "sources": [
      "src/native/binding.cc"
    ],
    "include_dirs": [
      "<!@(node -p \"require('node-addon-api').include\")",
      "runtimes/<!@(node -p \"process.platform + '-' + process.arch\")/native"
    ],
    "conditions": [
      ['OS=="linux"', {
        "conditions": [
          ['target_arch=="x64"', {
            "libraries": ["<(module_root_dir)/runtimes/linux-x64/native/librioc.so"],
            "copies": [{
              "destination": "<(module_root_dir)/build/Release",
              "files": ["<(module_root_dir)/runtimes/linux-x64/native/librioc.so"]
            }],
            "ldflags": ["-Wl,-rpath,'$$ORIGIN/../../runtimes/linux-x64/native'"]
          }],
          ['target_arch=="arm64"', {
            "libraries": ["<(module_root_dir)/runtimes/linux-arm64/native/librioc.so"],
            "copies": [{
              "destination": "<(module_root_dir)/build/Release",
              "files": ["<(module_root_dir)/runtimes/linux-arm64/native/librioc.so"]
            }],
            "ldflags": ["-Wl,-rpath,'$$ORIGIN/../../runtimes/linux-arm64/native'"]
          }]
        ]
      }],
      ['OS=="win"', {
        "conditions": [
          ['target_arch=="x64"', {
            "libraries": ["<(module_root_dir)/runtimes/win-x64/native/rioc.lib"],
            "copies": [{
              "destination": "<(module_root_dir)/build/Release",
              "files": ["<(module_root_dir)/runtimes/win-x64/native/rioc.dll"]
            }]
          }],
          ['target_arch=="arm64"', {
            "libraries": ["<(module_root_dir)/runtimes/win-arm64/native/rioc.lib"],
            "copies": [{
              "destination": "<(module_root_dir)/build/Release",
              "files": ["<(module_root_dir)/runtimes/win-arm64/native/rioc.dll"]
            }]
          }]
        ]
      }],
      ['OS=="mac"', {
        "conditions": [
          ['target_arch=="x64"', {
            "libraries": ["<(module_root_dir)/runtimes/osx-x64/native/librioc.dylib"],
            "copies": [{
              "destination": "<(module_root_dir)/build/Release",
              "files": ["<(module_root_dir)/runtimes/osx-x64/native/librioc.dylib"]
            }],
            "xcode_settings": {
              "OTHER_LDFLAGS": ["-Wl,-rpath,@loader_path/../../runtimes/osx-x64/native"]
            }
          }],
          ['target_arch=="arm64"', {
            "libraries": ["<(module_root_dir)/runtimes/osx-arm64/native/librioc.dylib"],
            "copies": [{
              "destination": "<(module_root_dir)/build/Release",
              "files": ["<(module_root_dir)/runtimes/osx-arm64/native/librioc.dylib"]
            }],
            "xcode_settings": {
              "OTHER_LDFLAGS": ["-Wl,-rpath,@loader_path/../../runtimes/osx-arm64/native"]
            }
          }]
        ]
      }]
    ],
    "dependencies": [
      "<!(node -p \"require('node-addon-api').gyp\")"
    ],
    "defines": [ "NAPI_DISABLE_CPP_EXCEPTIONS" ]
  }]
} 