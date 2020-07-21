#define QUOTE(name) #name
#define STR(macro) QUOTE(macro)
#undef LE_TOPO
#define LE_TOPO tree
#define MY_TOPO STR(LE_TOPO)
