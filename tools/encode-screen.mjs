// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
import {readFile, writeFile} from 'node:fs/promises';
import {encodeProject, decodeProject} from '../managed_components/nacboruiforlvgl/packages/project-codec/index.js';
import {compileNativeAppearance} from './native-appearance.mjs';
const project=JSON.parse(await readFile(new URL('../src/ui/project.json',import.meta.url),'utf8'));
const bytes=encodeProject(project);decodeProject(bytes);
await writeFile(new URL('../src/ui/project.cbor',import.meta.url),bytes);
await writeFile(new URL('../src/ui/appearance.cbor',import.meta.url),await compileNativeAppearance());
console.log(`${Object.keys(project.screens).length} screens; ${bytes.length} CBOR bytes`);
