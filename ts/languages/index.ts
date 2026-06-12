import { readFileSync } from 'fs';
import { join } from 'path';
import { addon } from '../index.js';

export function registerCppLanguage() {
    if (!addon) throw new Error("Codegraph-X addon not initialized");

    let cppModule;
    try {
        cppModule = require('tree-sitter-cpp');
    } catch (e) {
        console.warn("tree-sitter-cpp not found. C++ parsing will be disabled.");
        return;
    }

    let queryStr = "";
    try {
        const queryPath = join(__dirname, '..', '..', 'queries', 'cpp', 'tags.scm');
        queryStr = readFileSync(queryPath, 'utf8');
    } catch (e) {
        throw new Error(`Failed to load C++ tags.scm: ${e}`);
    }

    const cppConfig = {
        skipTypes: ['parameter_list', 'trailing_return_type'],
        acceptTypes: ['identifier', 'type_identifier', 'field_identifier', 'qualified_identifier']
    };

    addon.RegisterLanguage(".cpp", cppModule, queryStr, false, cppConfig);
    addon.RegisterLanguage(".hpp", cppModule, queryStr, false, cppConfig);
    addon.RegisterLanguage(".h", cppModule, queryStr, false, cppConfig);
    addon.RegisterLanguage(".cc", cppModule, queryStr, false, cppConfig);
    addon.RegisterLanguage(".cxx", cppModule, queryStr, false, cppConfig);
}

export function registerTypeScriptLanguage() {
    if (!addon) throw new Error("Codegraph-X addon not initialized");
    
    let tsModule;
    try {
        tsModule = require('tree-sitter-typescript');
    } catch (e) {
        console.warn("tree-sitter-typescript not found. TypeScript parsing will be disabled.");
        return;
    }

    let queryStr = "";
    try {
        const queryPath = join(__dirname, '..', '..', 'queries', 'typescript', 'tags.scm');
        queryStr = readFileSync(queryPath, 'utf8');
    } catch (e) {
        console.warn(`Failed to load TypeScript tags.scm: ${e}. TypeScript parsing disabled.`);
        return;
    }

    addon.RegisterLanguage(".ts", tsModule.typescript, queryStr, true, {});
    
    if (tsModule.tsx) {
        addon.RegisterLanguage(".tsx", tsModule.tsx, queryStr, true, {});
    }
}

export function registerJavaScriptLanguage() {
    if (!addon) throw new Error("Codegraph-X addon not initialized");
    
    let jsModule;
    try {
        jsModule = require('tree-sitter-javascript');
    } catch (e) {
        console.warn("tree-sitter-javascript not found. JavaScript parsing will be disabled.");
        return;
    }

    let queryStr = "";
    try {
        const queryPath = join(__dirname, '..', '..', 'queries', 'javascript', 'tags.scm');
        queryStr = readFileSync(queryPath, 'utf8');
    } catch (e) {
        console.warn(`Failed to load JavaScript tags.scm: ${e}. JavaScript parsing disabled.`);
        return;
    }

    addon.RegisterLanguage(".js", jsModule, queryStr, true, {});
    addon.RegisterLanguage(".jsx", jsModule, queryStr, true, {});
}

export function registerPythonLanguage() {
    if (!addon) throw new Error("Codegraph-X addon not initialized");
    
    let pyModule;
    try {
        pyModule = require('tree-sitter-python');
    } catch (e) {
        console.warn("tree-sitter-python not found. Python parsing will be disabled.");
        return;
    }

    let queryStr = "";
    try {
        const queryPath = join(__dirname, '..', '..', 'queries', 'python', 'tags.scm');
        queryStr = readFileSync(queryPath, 'utf8');
    } catch (e) {
        console.warn(`Failed to load Python tags.scm: ${e}. Python parsing disabled.`);
        return;
    }

    addon.RegisterLanguage(".py", pyModule, queryStr, true, {});
}

export function registerAllLanguages() {
    registerCppLanguage();
    registerTypeScriptLanguage();
    registerJavaScriptLanguage();
    registerPythonLanguage();
}
