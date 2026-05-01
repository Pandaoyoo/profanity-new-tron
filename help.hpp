#ifndef HPP_HELP
#define HPP_HELP

#include <string>

const std::string g_strHelp = \
"Usage: tron_vanity.exe [options]\n" \
"\n" \
"Modes:\n" \
"  --benchmark              Run benchmark\n" \
"  --zeros                  Search for addresses with leading zeros in hex (Starts with T1...)\n" \
"  --letters                Search for addresses with leading letters in hex\n" \
"  --numbers                Search for addresses with leading numbers in hex (default)\n" \
"  --leading <char>         Search for addresses starting with <char> in hex\n" \
"  --matching <hex>         Search for addresses matching <hex> pattern\n" \
"  -f, --file <path>        Load TRX addresses from file for vanity search\n" \
"  -t, --top <n>            Match first n characters of addresses from file\n" \
"  -w, --bottom <n>         Match last n characters of addresses from file\n" \
"\n" \
"Options:\n" \
"  -w, --work <size>        Local worksize (default: 64)\n" \
"  -W, --work-max <size>    Global worksize max\n" \
"  -s, --skip <index>       Skip device index\n" \
"  -i, --inverse-size <n>   Number of inversions to perform in a single batch (default: 255)\n" \
"  -I, --inverse-multiple <n> Multiple of batch inversions (default: 16384)\n" \
"\n" \
"Note: This tool searches based on the hexadecimal representation of the TRON address (after 0x41 prefix).\n" \
"The resulting address will always start with 'T' due to the 0x41 prefix.\n";

#endif
