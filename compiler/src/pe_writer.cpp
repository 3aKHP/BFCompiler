#include "pe_writer.h"
#include "pe_defs.h"
#include "pe_codegen.h"
#include <fstream>
#include <iostream>
#include <cstring>

namespace bf {

bool write_pe(const std::vector<IRInst>& program, const std::string& output_path) {
    const uint32_t FILE_ALIGN = 0x200;
    const uint32_t SECT_ALIGN = 0x1000;
    const uint64_t IMAGE_BASE = 0x0000000140000000ULL;
    const uint32_t NUM_SECTIONS = 3; // .text, .idata, .data

    // Calculate header size
    uint32_t headers_raw = sizeof(pe::DOS_HEADER) + sizeof(pe::NT_HEADERS64)
                         + NUM_SECTIONS * sizeof(pe::SECTION_HEADER);
    uint32_t headers_size = pe::align_up(headers_raw, FILE_ALIGN);

    // Section RVAs
    uint32_t text_rva  = SECT_ALIGN;           // 0x1000
    uint32_t idata_rva = text_rva + SECT_ALIGN; // placeholder, adjusted after code gen
    uint32_t data_rva;                          // after idata

    // --- Build .idata section ---
    // Import: kernel32.dll with 4 functions
    // Layout:
    //   [0..19]   Import Directory Entry for kernel32
    //   [20..39]  Null terminator entry
    //   [40..71]  ILT: 4 entries + null (5 * 8 = 40 bytes)
    //   [80..111] IAT: 4 entries + null (5 * 8 = 40 bytes) -- IAT at offset 80
    //   [120..]   Hint/Name entries + DLL name

    const char* func_names[] = {"GetStdHandle", "WriteFile", "ReadFile", "ExitProcess"};
    const char* dll_name = "kernel32.dll";

    // Pre-calculate idata offsets
    uint32_t ilt_off = 40;
    uint32_t iat_off = 80;
    uint32_t hints_off = 120;

    // Calculate hint/name table
    std::vector<uint8_t> idata;
    // Reserve space for directory + ILT + IAT
    idata.resize(hints_off);

    // Build hint/name entries
    uint32_t hint_positions[4];
    for (int i = 0; i < 4; ++i) {
        hint_positions[i] = (uint32_t)idata.size();
        uint16_t hint = 0;
        idata.push_back(hint & 0xFF);
        idata.push_back(hint >> 8);
        const char* n = func_names[i];
        while (*n) idata.push_back(*n++);
        idata.push_back(0);
        if (idata.size() % 2) idata.push_back(0); // align to 2
    }
    // DLL name
    uint32_t dll_name_off = (uint32_t)idata.size();
    { const char* n = dll_name; while (*n) idata.push_back(*n++); idata.push_back(0); }
    if (idata.size() % 2) idata.push_back(0);

    // Fill Import Directory Entry
    auto w32 = [&](size_t off, uint32_t v) {
        idata[off]=v&0xFF; idata[off+1]=(v>>8)&0xFF;
        idata[off+2]=(v>>16)&0xFF; idata[off+3]=(v>>24)&0xFF;
    };
    auto w64 = [&](size_t off, uint64_t v) {
        for (int i=0;i<8;++i) idata[off+i]=(v>>(i*8))&0xFF;
    };

    // We need to know idata_rva to fill RVAs. We'll do a two-pass:
    // First generate code with estimated idata_rva, then adjust.
    // Actually, code only references IAT and data via RIP-relative.
    // Let's fix layout first, then generate code.

    // Generate code with preliminary RVAs
    // text at 0x1000, we need code size to know idata_rva
    // But code size depends on program, not on section layout.
    // RIP-relative refs depend on exact RVAs though.
    // Solution: generate code twice, or pick idata_rva far enough.

    // Let's generate code once to get size, then fix RVAs, then regenerate.
    pe::CodeBuf dummy;
    uint32_t est_idata_rva = text_rva + SECT_ALIGN * 4; // generous estimate
    uint32_t est_data_rva  = est_idata_rva + SECT_ALIGN;
    uint32_t est_iat_rva   = est_idata_rva + iat_off;
    pe::gen_code(program, dummy, text_rva, est_iat_rva, est_data_rva);

    // Now we know code size
    uint32_t code_size = (uint32_t)dummy.size();
    uint32_t text_vsize = code_size;
    uint32_t text_raw = pe::align_up(code_size, FILE_ALIGN);

    // Real idata_rva
    idata_rva = text_rva + pe::align_up(text_vsize, SECT_ALIGN);
    uint32_t iat_rva_real = idata_rva + iat_off;
    uint32_t idata_vsize = (uint32_t)idata.size();
    uint32_t idata_raw = pe::align_up(idata_vsize, FILE_ALIGN);

    data_rva = idata_rva + pe::align_up(idata_vsize, SECT_ALIGN);
    uint32_t data_vsize = 30016; // tape(30000) + written(8) + readcnt(8)
    uint32_t data_raw = pe::align_up(data_vsize, FILE_ALIGN);

    // Regenerate code with correct RVAs
    pe::CodeBuf code;
    pe::gen_code(program, code, text_rva, iat_rva_real, data_rva);

    // Fill ILT and IAT with RVAs to hint/name entries
    for (int i = 0; i < 4; ++i) {
        uint64_t rva = idata_rva + hint_positions[i];
        w64(ilt_off + i * 8, rva);
        w64(iat_off + i * 8, rva);
    }
    // Null terminators already zero from resize

    // Fill Import Directory Entry
    w32(0, idata_rva + ilt_off);   // OriginalFirstThunk
    w32(4, 0);                      // TimeDateStamp
    w32(8, 0);                      // ForwarderChain
    w32(12, idata_rva + dll_name_off); // Name
    w32(16, idata_rva + iat_off);  // FirstThunk (IAT)
    // Entry at offset 20 is null terminator (already zero)

    // --- Build PE headers ---
    pe::DOS_HEADER dos{};
    dos.e_magic = 0x5A4D; // "MZ"
    dos.e_lfanew = sizeof(pe::DOS_HEADER);

    pe::NT_HEADERS64 nt{};
    nt.Signature = 0x00004550; // "PE\0\0"
    nt.FileHeader.Machine = 0x8664; // AMD64
    nt.FileHeader.NumberOfSections = NUM_SECTIONS;
    nt.FileHeader.SizeOfOptionalHeader = sizeof(pe::OPTIONAL_HEADER64);
    nt.FileHeader.Characteristics = 0x0022; // EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE

    auto& opt = nt.OptionalHeader;
    opt.Magic = 0x020B; // PE32+
    opt.MajorLinkerVersion = 1;
    opt.SizeOfCode = text_raw;
    opt.SizeOfInitializedData = idata_raw + data_raw;
    opt.AddressOfEntryPoint = text_rva;
    opt.BaseOfCode = text_rva;
    opt.ImageBase = IMAGE_BASE;
    opt.SectionAlignment = SECT_ALIGN;
    opt.FileAlignment = FILE_ALIGN;
    opt.MajorOSVersion = 6; opt.MinorOSVersion = 0;
    opt.MajorSubsystemVersion = 6; opt.MinorSubsystemVersion = 0;
    uint32_t image_end = data_rva + pe::align_up(data_vsize, SECT_ALIGN);
    opt.SizeOfImage = image_end;
    opt.SizeOfHeaders = headers_size;
    opt.Subsystem = 3; // CONSOLE
    opt.DllCharacteristics = 0x8160; // DYNAMIC_BASE|NX_COMPAT|TERMINAL_SERVER_AWARE|HIGH_ENTROPY_VA
    opt.SizeOfStackReserve = 0x100000;
    opt.SizeOfStackCommit = 0x1000;
    opt.SizeOfHeapReserve = 0x100000;
    opt.SizeOfHeapCommit = 0x1000;
    opt.NumberOfRvaAndSizes = 16;

    // Import table directory
    opt.DataDirectory[1].VirtualAddress = idata_rva;
    opt.DataDirectory[1].Size = 40; // 2 entries * 20 bytes
    // IAT directory
    opt.DataDirectory[12].VirtualAddress = idata_rva + iat_off;
    opt.DataDirectory[12].Size = 40; // 5 entries * 8 bytes

    // Section headers
    pe::SECTION_HEADER sects[3]{};

    // .text
    std::memcpy(sects[0].Name, ".text\0\0", 8);
    sects[0].VirtualSize = text_vsize;
    sects[0].VirtualAddress = text_rva;
    sects[0].SizeOfRawData = text_raw;
    sects[0].PointerToRawData = headers_size;
    sects[0].Characteristics = 0x60000020; // CODE|EXECUTE|READ

    // .idata
    std::memcpy(sects[1].Name, ".idata\0", 8);
    sects[1].VirtualSize = idata_vsize;
    sects[1].VirtualAddress = idata_rva;
    sects[1].SizeOfRawData = idata_raw;
    sects[1].PointerToRawData = headers_size + text_raw;
    sects[1].Characteristics = 0xC0000040; // INITIALIZED_DATA|READ|WRITE

    // .data
    std::memcpy(sects[2].Name, ".data\0\0", 8);
    sects[2].VirtualSize = data_vsize;
    sects[2].VirtualAddress = data_rva;
    sects[2].SizeOfRawData = data_raw;
    sects[2].PointerToRawData = headers_size + text_raw + idata_raw;
    sects[2].Characteristics = 0xC0000040; // INITIALIZED_DATA|READ|WRITE

    // --- Write file ---
    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
        std::cerr << "Error: cannot create '" << output_path << "'\n";
        return false;
    }

    // Write headers
    out.write((const char*)&dos, sizeof(dos));
    out.write((const char*)&nt, sizeof(nt));
    out.write((const char*)sects, sizeof(sects));

    // Pad headers to FILE_ALIGN
    uint32_t hdr_written = sizeof(dos) + sizeof(nt) + sizeof(sects);
    std::vector<uint8_t> pad(headers_size - hdr_written, 0);
    out.write((const char*)pad.data(), pad.size());

    // .text section
    out.write((const char*)code.data.data(), code.data.size());
    if (text_raw > code.data.size()) {
        std::vector<uint8_t> tp(text_raw - code.data.size(), 0);
        out.write((const char*)tp.data(), tp.size());
    }

    // .idata section
    out.write((const char*)idata.data(), idata.size());
    if (idata_raw > idata.size()) {
        std::vector<uint8_t> ip(idata_raw - idata.size(), 0);
        out.write((const char*)ip.data(), ip.size());
    }

    // .data section
    std::vector<uint8_t> data_sect(data_raw, 0);
    out.write((const char*)data_sect.data(), data_sect.size());

    out.close();
    return true;
}

} // namespace bf
