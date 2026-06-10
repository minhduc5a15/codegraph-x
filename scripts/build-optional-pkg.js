const fs = require('fs');
const path = require('path');
const os = require('os');

const platform = os.platform();
const arch = os.arch();
const platformKey = `${platform}-${arch}`;

// The compiled node module
const buildPath = path.join(__dirname, '..', 'build', 'codegraph_addon.node');
const releasePath = path.join(__dirname, '..', 'build', 'Release', 'codegraph_addon.node');

let sourceFile = '';
if (fs.existsSync(releasePath)) {
  sourceFile = releasePath;
} else if (fs.existsSync(buildPath)) {
  sourceFile = buildPath;
} else {
  console.error(`Could not find compiled addon for ${platformKey}`);
  process.exit(1);
}

const npmDir = path.join(__dirname, '..', 'npm', platformKey);
if (!fs.existsSync(npmDir)) {
  fs.mkdirSync(npmDir, { recursive: true });
}

// Copy the binary
const targetFile = path.join(npmDir, 'codegraph_addon.node');
fs.copyFileSync(sourceFile, targetFile);

// Generate package.json
const pkgJson = {
  name: `@codegraph-x/core-${platformKey}`,
  version: require('../package.json').version,
  os: [platform],
  cpu: [arch],
  main: 'codegraph_addon.node',
  description: `Codegraph-X native binary for ${platformKey}`,
};

fs.writeFileSync(path.join(npmDir, 'package.json'), JSON.stringify(pkgJson, null, 2));

console.log(`Successfully built optional package for ${platformKey} at ${npmDir}`);
