#!/usr/bin/env node

const fs = require('fs');
const path = require('path');

// Opcode definitions
const OPCODES = {
    'push':      0x01,
    'i32.add':   0x02,
    'add':       0x02,
    'i32.sub':   0x03,
    'sub':       0x03,
    'i32.mul':   0x04,
    'mul':       0x04,
    'drop':      0x05,
    'print':     0x08,
    
    // Comparison operations
    'i32.eq':    0x09,
    'eq':        0x09,
    'i32.lt_s':  0x0A,
    'lt_s':      0x0A,
    'i32.gt_s':  0x0B,
    'gt_s':      0x0B,
    'i32.lt_u':  0x0C,
    'lt_u':      0x0C,
    'i32.gt_u':  0x0D,
    'gt_u':      0x0D,
    
    // Control flow
    'br_if':     0x0E,
    'jump':      0x0F,
    'call':      0x10,
    'return':    0x11,
    
    // Stack operations
    'dup':       0x12,
    'swap':      0x13,
    'over':      0x14,
    'rot':       0x15,
    
    // Bitwise operations
    'i32.and':   0x16,
    'and':       0x16,
    'i32.or':    0x17,
    'or':        0x17,
    'i32.xor':   0x18,
    'xor':       0x18,
    'i32.not':   0x19,
    'not':       0x19,
    'i32.shl':   0x1A,
    'shl':       0x1A,
    'i32.shr_u': 0x1B,
    'shr_u':     0x1B,
    'i32.shr_s': 0x1C,
    'shr_s':     0x1C,
    
    // Memory operations
    'load':      0x1D,
    'store':     0x1E,
    
    // I/O operations
    'key':       0x1F,
    
    // Return stack operations
    '>r':        0x30,
    'r>':        0x31,
    'r@':        0x32,
    'depth':     0x33,
    'rdepth':    0x34,
    'eqz':       0x35,
    
    'halt':      0xFF
};

function parseImmediate(value) {
    // Parse immediate value (decimal or hex)
    let num = value.startsWith('0x') ? parseInt(value, 16) : parseInt(value, 10);
    
    // Convert to 32-bit little-endian bytes
    const bytes = [];
    for (let i = 0; i < 4; i++) {
        bytes.push(num & 0xFF);
        num >>= 8;
    }
    return bytes;
}

function assemble(source) {
    const lines = source.split('\n');
    const bytecode = [];
    const labels = {};
    
    // First pass: collect labels
    let bytePos = 0;
    for (let lineNum = 0; lineNum < lines.length; lineNum++) {
        let line = lines[lineNum].trim();
        
        // Remove comments
        const commentIdx = line.indexOf(';');
        if (commentIdx >= 0) {
            line = line.substring(0, commentIdx).trim();
        }
        
        if (line === '') continue;
        
        // Check for label
        if (line.startsWith(':')) {
            const label = line.substring(1).trim();
            labels[label] = bytePos;
            continue;
        }
        
        // Calculate instruction size
        const parts = line.split(/\s+/);
        const instruction = parts[0].toLowerCase();
        
        if (!OPCODES.hasOwnProperty(instruction)) {
            throw new Error(`Unknown instruction '${instruction}' at line ${lineNum + 1}`);
        }
        
        bytePos += 1; // opcode
        
        if (instruction === 'push' || instruction === 'br_if' || instruction === 'jump' || instruction === 'call') {
            bytePos += 4; // 32-bit immediate
        } else if (instruction === 'local.get' || instruction === 'local.set') {
            bytePos += 1; // 8-bit index
        }
    }
    
    // Second pass: generate bytecode
    for (let lineNum = 0; lineNum < lines.length; lineNum++) {
        let line = lines[lineNum].trim();
        
        // Remove comments
        const commentIdx = line.indexOf(';');
        if (commentIdx >= 0) {
            line = line.substring(0, commentIdx).trim();
        }
        
        if (line === '') continue;
        
        // Skip labels
        if (line.startsWith(':')) continue;
        
        // Split instruction and operands
        const parts = line.split(/\s+/);
        const instruction = parts[0].toLowerCase();
        
        if (!OPCODES.hasOwnProperty(instruction)) {
            throw new Error(`Unknown instruction '${instruction}' at line ${lineNum + 1}`);
        }
        
        const opcode = OPCODES[instruction];
        bytecode.push(opcode);
        
        // Handle instructions with operands
        if (instruction === 'push') {
            if (parts.length < 2) {
                throw new Error(`PUSH requires an immediate value at line ${lineNum + 1}`);
            }
            const immBytes = parseImmediate(parts[1]);
            bytecode.push(...immBytes);
        } else if (instruction === 'local.get' || instruction === 'local.set') {
            if (parts.length < 2) {
                throw new Error(`${instruction.toUpperCase()} requires an index at line ${lineNum + 1}`);
            }
            const index = parseInt(parts[1], 10);
            bytecode.push(index);
        } else if (instruction === 'br_if' || instruction === 'jump' || instruction === 'call') {
            if (parts.length < 2) {
                throw new Error(`${instruction.toUpperCase()} requires an address at line ${lineNum + 1}`);
            }
            
            // Check if operand is a label
            let target = parts[1];
            // Strip leading ':' from label reference
            if (target.startsWith(':')) {
                target = target.substring(1);
            }
            if (labels.hasOwnProperty(target)) {
                target = labels[target].toString();
            }
            
            const immBytes = parseImmediate(target);
            bytecode.push(...immBytes);
        }
    }
    
    return bytecode;
}

function generateVerilogInit(bytecode, outputFile) {
    let verilogCode = `// Auto-generated by assembler.js\n`;
    verilogCode += `// Total bytes: ${bytecode.length}\n\n`;
    verilogCode += `initial begin\n`;
    
    for (let i = 0; i < bytecode.length; i++) {
        verilogCode += `    program[${i.toString().padStart(3, ' ')}] = 8'h${bytecode[i].toString(16).padStart(2, '0').toUpperCase()};\n`;
    }
    
    verilogCode += `end\n`;
    
    fs.writeFileSync(outputFile, verilogCode);
    console.log(`Generated Verilog initialization: ${outputFile}`);
}

function generateHexDump(bytecode, outputFile) {
    let hexDump = '';
    
    for (let i = 0; i < bytecode.length; i += 16) {
        const chunk = bytecode.slice(i, i + 16);
        hexDump += chunk.map(b => b.toString(16).padStart(2, '0')).join(' ') + '\n';
    }
    
    fs.writeFileSync(outputFile, hexDump);
    console.log(`Generated hex dump: ${outputFile}`);
}

function generateBinary(bytecode, outputFile) {
    const buffer = Buffer.from(bytecode);
    fs.writeFileSync(outputFile, buffer);
    console.log(`Generated binary file: ${outputFile} (${bytecode.length} bytes)`);
}

// Main
if (process.argv.length < 3) {
    console.log('Usage: node assembler.js <input.asm> [output.hex]');
    console.log('');
    console.log('Supported instructions:');
    for (const [mnemonic, opcode] of Object.entries(OPCODES)) {
        console.log(`  ${mnemonic.padEnd(12)} 0x${opcode.toString(16).padStart(2, '0')}`);
    }
    process.exit(1);
}

const inputFile = process.argv[2];
const outputBase = process.argv[3] || inputFile.replace(/\.\w+$/, '');

try {
    const source = fs.readFileSync(inputFile, 'utf8');
    const bytecode = assemble(source);
    
    console.log(`Assembled ${bytecode.length} bytes from ${inputFile}`);
    
    generateHexDump(bytecode, outputBase + '.hex');
    generateVerilogInit(bytecode, outputBase + '.vh');
    generateBinary(bytecode, outputBase + '.bin');
    
} catch (error) {
    console.error(`Error: ${error.message}`);
    process.exit(1);
}
