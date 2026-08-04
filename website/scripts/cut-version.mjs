import { execSync } from 'node:child_process';
import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

const scriptDir = dirname(fileURLToPath(import.meta.url));
const siteDir = resolve(scriptDir, '..');
const configPath = resolve(siteDir, 'docusaurus.config.js');
const versionsPath = resolve(siteDir, 'versions.json');

const SEMVER = /^\d+\.\d+\.\d+$/;

function die(msg) {
  console.error(`\x1b[31mcut-version: ${msg}\x1b[0m`);
  process.exit(1);
}

function usage() {
  console.log('Usage: npm run cut-version <version> [nextDevVersion]');
  console.log('  <version>         the release you are freezing, e.g. 0.6.0');
  console.log('  [nextDevVersion]  what docs/ becomes next (default: bump minor)');
  console.log('');
  console.log('Freezes docs/ into versioned_docs/version-<version>/, makes it the');
  console.log('default served version, and relabels docs/ as the next dev version.');
}

const version = process.argv[2];
const nextArg = process.argv[3];

if (!version || version === '--help' || version === '-h') {
  usage();
  process.exit(version ? 0 : 1);
}
if (!SEMVER.test(version)) die(`"${version}" is not X.Y.Z`);
if (nextArg && !SEMVER.test(nextArg)) die(`"${nextArg}" is not X.Y.Z`);

const [maj, min] = version.split('.').map(Number);
const nextDev = nextArg ?? `${maj}.${min + 1}.0`;
if (nextDev === version) die('next dev version must differ from the released version');

if (existsSync(versionsPath)) {
  const existing = JSON.parse(readFileSync(versionsPath, 'utf8'));
  if (existing.includes(version)) die(`version ${version} already exists in versions.json`);
}

let config = readFileSync(configPath, 'utf8');

const lastVersionRe = /lastVersion:\s*'[^']*'/;
const currentBlockRe = /(current:\s*\{)([\s\S]*?)(\n\s*\},)/;
if (!lastVersionRe.test(config)) die('could not find lastVersion in docusaurus.config.js');
if (!currentBlockRe.test(config)) die('could not find versions.current block in docusaurus.config.js');

console.log(`\x1b[36m▸ freezing docs/ as version ${version}\x1b[0m`);
execSync(`npx docusaurus docs:version ${version}`, { cwd: siteDir, stdio: 'inherit' });

config = config.replace(lastVersionRe, `lastVersion: '${version}'`);
config = config.replace(currentBlockRe, (_m, open, body, close) => {
  const patched = body
    .replace(/label:\s*'[^']*'/, `label: '${nextDev}'`)
    .replace(/path:\s*'[^']*'/, `path: '${nextDev}'`);
  return `${open}${patched}${close}`;
});
writeFileSync(configPath, config);

try {
  execSync(`node --input-type=module -e "await import('file://${configPath.replace(/\\/g, '/')}')"`, {
    cwd: siteDir,
    stdio: 'ignore',
  });
} catch {
  die('docusaurus.config.js failed to load after patching. Check the diff and revert.');
}

console.log('');
console.log(`\x1b[32m✓ cut ${version}\x1b[0m`);
console.log(`  default (served at /):   ${version}`);
console.log(`  docs/ is now:            ${nextDev} (unreleased)`);
console.log('');
console.log('Next:');
console.log('  git diff docusaurus.config.js versions.json   # review');
console.log('  npm run build                                 # catch broken links');
console.log(`  git add . && git commit -m "docs: cut ${version}"`);
