import * as fs from 'fs';
import * as path from 'path';

export function readJson(filePath: string): any {
  if (!fs.existsSync(filePath)) {
    return {};
  }
  try {
    const data = fs.readFileSync(filePath, 'utf-8');
    return JSON.parse(data);
  } catch (e) {
    return {};
  }
}

export function writeJson(filePath: string, data: any): void {
  const dir = path.dirname(filePath);
  if (!fs.existsSync(dir)) {
    fs.mkdirSync(dir, { recursive: true });
  }
  fs.writeFileSync(filePath, JSON.stringify(data, null, 2), 'utf-8');
}

/**
 * Updates a deeply nested key in a JSON object.
 * e.g. path = "mcpServers.codegraph-x"
 */
export function setDeepValue(obj: any, keyPath: string, value: any): void {
  const parts = keyPath.split('.');
  let current = obj;
  for (let i = 0; i < parts.length - 1; i++) {
    const part = parts[i];
    if (typeof current[part] !== 'object' || current[part] === null) {
      current[part] = {};
    }
    current = current[part];
  }
  current[parts[parts.length - 1]] = value;
}

/**
 * Checks if deep value exists
 */
export function getDeepValue(obj: any, keyPath: string): any {
  const parts = keyPath.split('.');
  let current = obj;
  for (const part of parts) {
    if (typeof current !== 'object' || current === null) return undefined;
    current = current[part];
  }
  return current;
}
