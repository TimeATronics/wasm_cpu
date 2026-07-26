import sys
data = open(sys.argv[1], "rb").read()
OP_NAMES = {
    0x01:"push",0x02:"add",0x03:"sub",0x04:"mul",0x05:"drop",0x08:"print",
    0x09:"eq",0x0A:"lt_s",0x0B:"gt_s",0x0C:"lt_u",0x0D:"gt_u",
    0x0E:"br_if",0x0F:"jump",0x10:"call",0x11:"return",
    0x12:"dup",0x13:"swap",0x14:"over",0x15:"rot",
    0x16:"and",0x17:"or",0x18:"xor",0x19:"not",
    0x1A:"shl",0x1B:"shr_u",0x1C:"shr_s",0x1D:"load",0x1E:"store",
    0x1F:"key",0x30:">r",0x31:"r>",0x32:"r@",
    0x33:"depth",0x34:"rdepth",0x35:"eqz",
    0x36:"div_s",0x37:"load8_u",0x38:"store8",
    0x39:"local.get",0x3A:"local.set",0xFF:"halt"
}
i = 0
while i < len(data):
    op = data[i]
    name = OP_NAMES.get(op, f"???0x{op:02X}")
    if op == 0x01:  # push
        val = data[i+1] | (data[i+2]<<8) | (data[i+3]<<16) | (data[i+4]<<24)
        print(f"  {i:04X}: push {val} (0x{val:08X})")
        i += 5
    elif op in (0x0E, 0x0F, 0x10):  # br_if, jump, call
        target = data[i+1] | (data[i+2]<<8) | (data[i+3]<<16) | (data[i+4]<<24)
        print(f"  {i:04X}: {name} -> 0x{target:04X}")
        i += 5
    elif op in (0x39, 0x3A):  # local.get, local.set
        idx = data[i+1]
        print(f"  {i:04X}: {name} {idx}")
        i += 2
    elif op == 0xFF:
        print(f"  {i:04X}: halt")
        i += 1
    else:
        print(f"  {i:04X}: {name}")
        i += 1
