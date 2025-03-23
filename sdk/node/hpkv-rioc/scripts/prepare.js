const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

// Paths
const rootDir = path.resolve(__dirname, '..');
const riocDir = path.resolve(rootDir, '../../../rioc/build');
const runtimesDir = path.resolve(rootDir, 'runtimes');

// Platform-specific library names
const libraryNames = {
  linux: {
    'x64': { source: 'librioc.so', target: 'librioc.so' },
    'arm64': { source: 'librioc.so', target: 'librioc.so' }
  },
  win32: {
    'x64': { source: 'rioc.dll', target: 'rioc.dll', lib: { source: 'rioc.lib', target: 'rioc.lib' } },
    'arm64': { source: 'rioc.dll', target: 'rioc.dll', lib: { source: 'rioc.lib', target: 'rioc.lib' } }
  },
  darwin: {
    'x64': { source: 'librioc.dylib', target: 'librioc.dylib' },
    'arm64': { source: 'librioc.dylib', target: 'librioc.dylib' }
  }
};

// Create runtimes directory if it doesn't exist
if (!fs.existsSync(runtimesDir)) {
  fs.mkdirSync(runtimesDir, { recursive: true });
}

// Copy libraries for each platform
Object.entries(libraryNames).forEach(([platform, architectures]) => {
  Object.entries(architectures).forEach(([arch, { source, target, lib }]) => {
    const runtimePath = path.join(runtimesDir, `${platform === 'win32' ? 'win' : platform === 'darwin' ? 'osx' : platform}-${arch}`, 'native');
    const sourcePath = path.join(riocDir, source);
    const targetPath = path.join(runtimePath, target);

    // Create runtime directory if it doesn't exist
    if (!fs.existsSync(runtimePath)) {
      fs.mkdirSync(runtimePath, { recursive: true });
    }

    // Copy library if it exists
    if (fs.existsSync(sourcePath)) {
      fs.copyFileSync(sourcePath, targetPath);
      console.log(`Copied ${sourcePath} to ${targetPath}`);
    } else {
      console.warn(`Warning: ${sourcePath} does not exist`);
    }

    // For Windows, also copy the .lib file
    if (lib) {
      const libSourcePath = path.join(riocDir, lib.source);
      const libTargetPath = path.join(runtimePath, lib.target);
      if (fs.existsSync(libSourcePath)) {
        fs.copyFileSync(libSourcePath, libTargetPath);
        console.log(`Copied ${libSourcePath} to ${libTargetPath}`);
      } else {
        console.warn(`Warning: ${libSourcePath} does not exist`);
      }
    }
  });
});

console.log('Prepare script completed successfully');