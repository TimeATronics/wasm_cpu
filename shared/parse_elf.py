import struct
import sys

def parse_elf(path):
    with open(path, 'rb') as f:
        data = f.read()
    
    ehdr = struct.unpack_from('16sHHIIIIIHHHHHH', data, 0)
    e_type = ehdr[1]
    e_machine = ehdr[2]
    e_shoff = ehdr[6]
    e_shentsize = ehdr[11]
    e_shnum = ehdr[12]
    e_shstrndx = ehdr[13]
    
    print('e_type=%d e_machine=0x%04X e_shoff=%d e_shnum=%d' % (e_type, e_machine, e_shoff, e_shnum))
    
    shdr0 = struct.unpack_from('IIIIIIIIII', data, e_shoff + e_shstrndx * e_shentsize)
    shstrtab = data[shdr0[4]:shdr0[4]+shdr0[5]]
    
    for i in range(e_shnum):
        shdr = struct.unpack_from('IIIIIIIIII', data, e_shoff + i * e_shentsize)
        sh_name, sh_type, sh_flags, sh_addr, sh_offset = shdr[0:5]
        sh_size, sh_link, sh_info, sh_addralign, sh_entsize = shdr[5:10]
        
        if sh_name < len(shstrtab):
            end = shstrtab.index(b'\x00', sh_name)
            name = shstrtab[sh_name:end].decode()
        else:
            name = '?'
        
        print('  [%d] %-15s type=%d flags=0x%X offset=%d size=%d' % (i, name, sh_type, sh_flags, sh_offset, sh_size))
        
        if sh_type == 2:  # SHT_SYMTAB
            syms = sh_size // sh_entsize
            print('       symtab: %d entries link=%d info=%d' % (syms, sh_link, sh_info))
            strtab_shdr = struct.unpack_from('IIIIIIIIII', data, e_shoff + sh_link * e_shentsize)
            strtab = data[strtab_shdr[4]:strtab_shdr[4]+strtab_shdr[5]]
            
            for j in range(syms):
                sym = struct.unpack_from('IIIBBH', data, sh_offset + j * sh_entsize)
                sym_name, sym_value, sym_size, sym_info, sym_other, sym_shndx = sym
                
                if sym_name > 0 and sym_name < len(strtab):
                    n_end = strtab.index(b'\x00', sym_name)
                    sname = strtab[sym_name:n_end].decode()
                else:
                    sname = ''
                
                bind = sym_info >> 4
                stype = sym_info & 0xF
                print('       [%d] "%s" value=%d bind=%d type=%d shndx=%d' % (j, sname, sym_value, bind, stype, sym_shndx))
        
        if sh_type == 4:  # SHT_RELA
            relas = sh_size // sh_entsize
            print('       rela: %d entries link=%d info=%d' % (relas, sh_link, sh_info))
            
            for j in range(relas):
                rela = struct.unpack_from('III', data, sh_offset + j * sh_entsize)
                r_offset, r_info, r_addend = rela
                r_sym = r_info >> 8
                r_type = r_info & 0xFF
                print('       [%d] offset=%d sym=%d type=%d addend=%d' % (j, r_offset, r_sym, r_type, r_addend))

if __name__ == '__main__':
    parse_elf(sys.argv[1])
