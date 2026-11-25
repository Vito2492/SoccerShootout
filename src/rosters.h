/* Dynamic in-memory roster tables for Capcom's Soccer Shootout (USA) (SNES, 1994).
 * Table sits uncompressed at ROM offset 0x03688A: 12 teams x 20 players x 12 bytes.
 * Encoding: 'A'..'Z' -> 0x00..0x19, 'a'..'z' -> 0x20..0x39, space/pad -> 0x7F.
 * The ROM file on disk is never modified; selected roster pack is applied in memory.
 */
#ifndef SOCCER_ROSTERS_H
#define SOCCER_ROSTERS_H

#include <stdint.h>
#include <stddef.h>

#define ROSTER_ROM_OFFSET 0x03688Au  // Table 1: In-match pitch HUD names (12 bytes/player)
#define ROSTER_T2_BASE    0x09C033u  // Table 2: Lineup/Reserve screen names (15 bytes/player, 12-byte name)
#define ROSTER_T2_STRIDE  15u
#define ROSTER_T2_SIZE    (12u * 20u * 15u)
#define ROSTER_TEAMS      12
#define ROSTER_PLAYERS    20
#define ROSTER_NAME_LEN   12

// Exact Table 2 offset per team (Table 2 follows the 4x3 flag grid order)
static const uint32_t roster_t2_offsets[ROSTER_TEAMS] = {
    0x09C033u, // 0: U.S.A.
    0x09C15Fu, // 1: BRAZIL
    0x09C28Bu, // 2: RUSSIA
    0x09C3B7u, // 3: ARGENTINA
    0x09C4E3u, // 4: MEXICO
    0x09C60Fu, // 5: FRANCE
    0x09C867u, // 6: GERMANY
    0x09CABFu, // 7: HOLLAND
    0x09CBEBu, // 8: ITALY
    0x09CD17u, // 9: CAMEROON
    0x09C993u, // 10: SPAIN
    0x09C73Bu, // 11: GREAT BRITAIN
};

static const char *const roster_team_names[ROSTER_TEAMS] = {
    "U.S.A.", "BRAZIL", "RUSSIA", "ARGENTINA", "MEXICO", "FRANCE",
    "GERMANY", "HOLLAND", "ITALY", "CAMEROON", "SPAIN", "GREAT BRITAIN"
};

// Pack 1: Modern 2026 National Squads (12 teams x 20 players)
// Slots: 0=GK1, 1-4=DEF, 5-8=MID, 9-10=FW, 11-14=SUBS, 15=GK2, 16-19=RESERVES
static const char *const rosters_2026[ROSTER_TEAMS][ROSTER_PLAYERS] = {
    // 0: U.S.A.
    { "Turner", "Dest", "Richards", "Ream", "Robinson", "Adams", "McKennie", "Musah", "Reyna", "Pulisic",
      "Balogun", "Weah", "Aaronson", "Pepi", "Tillman", "Horvath", "Scally", "Cardoso", "Sargent", "Wright" },
    // 1: BRAZIL
    { "Alisson", "Danilo", "Marquinhos", "Gabriel", "Arana", "Guimaraes", "Paqueta", "Gerson", "Rodrygo", "Raphinha",
      "Vinicius", "Endrick", "Savinho", "Estevao", "Martinelli", "Ederson", "Militao", "Bremer", "Andre", "Cunha" },
    // 2: RUSSIA
    { "Safonov", "Karavaev", "Diveev", "Osipenko", "Krugovoy", "Barinov", "Golovin", "Miranchuk", "Batrakov", "Tyukavin",
      "Chalov", "Glushenkov", "Oblyakov", "Kuzyaev", "Glebov", "Lunev", "Silyanov", "Morozov", "Zakharyan", "Musaev" },
    // 3: ARGENTINA
    { "Martinez", "Molina", "Romero", "Lisandro", "Tagliafico", "De Paul", "Enzo", "MacAllister", "Messi", "Lautaro",
      "Julian", "Garnacho", "Lo Celso", "Paredes", "Nico Paz", "Rulli", "Otamendi", "Montiel", "Almada", "Simeone" },
    // 4: MEXICO
    { "Malagon", "Sanchez", "Montes", "Vasquez", "Gallardo", "Alvarez", "Chavez", "Romo", "Vega", "Jimenez",
      "Gimenez", "Lozano", "Alvarado", "Pineda", "Quinones", "Ochoa", "Reyes", "Araujo", "Ruiz", "Huerta" },
    // 5: FRANCE
    { "Maignan", "Kounde", "Saliba", "Konate", "Theo", "Tchouameni", "Camavinga", "Rabiot", "Olise", "Dembele",
      "Mbappe", "Barcola", "Thuram", "Kolo Muani", "Griezmann", "Samba", "Upamecano", "Pavard", "ZaireEmery", "Doue" },
    // 6: GERMANY
    { "Ter Stegen", "Kimmich", "Rudiger", "Tah", "Mittelstadt", "Andrich", "Pavlovic", "Wirtz", "Musiala", "Havertz",
      "Fullkrug", "Sane", "Gnabry", "Brandt", "Undav", "Baumann", "Schlotter", "Raum", "Gross", "Beier" },
    // 7: HOLLAND (NETHERLANDS)
    { "Verbruggen", "Dumfries", "Van Dijk", "Van de Ven", "Ake", "De Jong", "Gravenberch", "Reijnders", "Simons", "Gakpo",
      "Depay", "Frimpong", "Malen", "Zirkzee", "Weghorst", "Flekken", "De Ligt", "Timber", "Koopmeiners", "Brobbey" },
    // 8: ITALY
    { "Donnarumma", "Di Lorenzo", "Bastoni", "Calafiori", "Dimarco", "Barella", "Tonali", "Frattesi", "Pellegrini", "Retegui",
      "Kean", "Cambiaso", "Raspadori", "Ricci", "Chiesa", "Vicario", "Buongiorno", "Udogie", "Rovella", "Maldini" },
    // 9: CAMEROON
    { "Onana", "Tchatchoua", "Castelletto", "Wooh", "Tolo", "Anguissa", "Baleba", "Hongla", "Mbeumo", "Aboubakar",
      "Bassogog", "Nkoudou", "Ngamaleu", "Magri", "Choupo", "Epassy", "Ngadeu", "Fai", "Ntcham", "Ateba" },
    // 10: SPAIN
    { "Unai Simon", "Carvajal", "Le Normand", "Cubarsi", "Cucurella", "Rodri", "Pedri", "Fabian", "Yamal", "Williams",
      "Morata", "Olmo", "Gavi", "Merino", "Oyarzabal", "Raya", "Laporte", "Grimaldo", "Zubimendi", "Ferran" },
    // 11: GREAT BRITAIN (ENGLAND)
    { "Pickford", "Trent AA", "Stones", "Guehi", "LewisSkelly", "Rice", "Bellingham", "Palmer", "Saka", "Foden",
      "Kane", "Watkins", "Gordon", "Mainoo", "Bowen", "Ramsdale", "Walker", "Colwill", "Eze", "Madueke" },
};

// Pack 2: Real Historical 1994 Legends (USA '94 real names replacing Capcom's unlicensed aliases)
static const char *const rosters_1994[ROSTER_TEAMS][ROSTER_PLAYERS] = {
    // 0: U.S.A. '94
    { "Meola", "Caligiuri", "Lalas", "Balboa", "Dooley", "Harkes", "Ramos", "Sorber", "Wynalda", "Stewart",
      "Perez", "Clavijo", "Jones", "Wegerle", "Burns", "Friedel", "Klopas", "Reyna", "Moore", "Sommer" },
    // 1: BRAZIL '94
    { "Taffarel", "Jorginho", "Aldair", "Marcio", "Branco", "Mauro Silva", "Rai", "Dunga", "Zinho", "Bebeto",
      "Romario", "Cafu", "Mazinho", "Muller", "Viola", "Zetti", "Leonardo", "Ricardo", "PauloSergio", "Ronaldo" },
    // 2: RUSSIA '94
    { "Cherchesov", "Khlestov", "Onopko", "Gorlukovich", "Nikiforov", "Karpin", "Lediakhov", "Shalimov", "Mostovoi", "Salenko",
      "Radchenko", "Yuran", "Kornienko", "Piatnitski", "Popov", "Kharine", "Ternavski", "Borodyuk", "Galiamin", "Tsymbalar" },
    // 3: ARGENTINA '94
    { "Goycochea", "Chamot", "Ruggeri", "Vazquez", "Sensini", "Redondo", "Simeone", "Maradona", "Balbo", "Caniggia",
      "Batistuta", "Ortega", "Caceres", "MedinaBello", "Mancuso", "Islas", "Diaz", "Basualdo", "Rodriguez", "Scoponi" },
    // 4: MEXICO '94
    { "Campos", "Suarez", "Ramirez", "Ambriz", "Gutierrez", "Bernal", "Garcia Aspe", "Galindo", "Alves", "HugoSanchez",
      "Garcia", "Hermosillo", "Valdez", "Del Olmo", "Rodriguez", "Chavez", "Perales", "Salvador", "Salgado", "Fernandez" },
    // 5: FRANCE '94
    { "Lama", "Angloma", "Blanc", "Desailly", "Lizarazu", "Deschamps", "Petit", "Djorkaeff", "Ginola", "Papin",
      "Cantona", "Zidane", "Guerin", "Sauzee", "Roche", "Martini", "Di Meco", "Pedros", "Le Guen", "Dugarry" },
    // 6: GERMANY '94
    { "Kopke", "Matthaus", "Kohler", "Helmer", "Brehme", "Sammer", "Hassler", "Moller", "Effenberg", "Klinsmann",
      "Voller", "Riedle", "Basler", "Buchwald", "Strunz", "Illgner", "Berthold", "Ziege", "Kuntz", "Wagner" },
    // 7: HOLLAND '94
    { "De Goey", "Koeman", "F De Boer", "De Wolf", "Rijkaard", "Winter", "Jonk", "Overmars", "Wouters", "Bergkamp",
      "Roy", "R De Boer", "Taument", "Van Vossen", "Bosman", "Van der Sar", "Valckx", "Blind", "Numan", "Van Gobbel" },
    // 8: ITALY '94
    { "Pagliuca", "Baresi", "Maldini", "Costacurta", "Tassotti", "Albertini", "D Baggio", "Donadoni", "Berti", "R Baggio",
      "Signori", "Massaro", "Casiraghi", "Zola", "Benarrivo", "Marchegiani", "Mussi", "Conte", "Evani", "Apolloni" },
    // 9: CAMEROON '94
    { "Bell", "Tataw", "Song", "Ndip", "Kana Biyik", "Mbouh", "Mfede", "Libiih", "Omam Biyik", "Milla",
      "Tchami", "Agbo", "Kalla", "Foe", "Embe", "Songo O", "Nkontchou", "Embou", "Mabouang", "Nkong" },
    // 10: SPAIN '94
    { "Zubizarreta", "Ferrer", "Alkorta", "Abelardo", "Sergi", "Nadal", "Hierro", "Guardiola", "LuisEnrique", "Salinas",
      "Caminero", "Bakero", "Goikoetxea", "Begiristain", "Guerrero", "Canizares", "Camarasa", "Voro", "Juanele", "Lopetegui" },
    // 11: GREAT BRITAIN '94
    { "Seaman", "Dixon", "Adams", "Pallister", "Pearce", "Ince", "Platt", "Gascoigne", "Barnes", "Shearer",
      "Wright", "Sheringham", "Ferdinand", "McManaman", "Batty", "Flowers", "Keown", "Le Saux", "Beardsley", "Anderton" },
};

static inline void apply_roster_pack(uint8_t *rom_buf, const uint8_t *orig_t1, const uint8_t *orig_t2, int pack)
{
    if (!rom_buf || !orig_t1 || !orig_t2) return;
    if (pack == 0) {
        // Pack 0: Original Capcom 1994 ROM bytes (both pitch HUD & lineup screen tables)
        memcpy(rom_buf + ROSTER_ROM_OFFSET, orig_t1, ROSTER_TEAMS * ROSTER_PLAYERS * ROSTER_NAME_LEN);
        memcpy(rom_buf + ROSTER_T2_BASE, orig_t2, ROSTER_T2_SIZE);
        return;
    }
    const char *const (*src)[ROSTER_PLAYERS] = (pack == 1) ? rosters_2026 : rosters_1994;
    for (int t = 0; t < ROSTER_TEAMS; t++) {
        for (int p = 0; p < ROSTER_PLAYERS; p++) {
            uint8_t *d1 = rom_buf + ROSTER_ROM_OFFSET + (t * ROSTER_PLAYERS + p) * ROSTER_NAME_LEN;
            uint8_t *d2 = rom_buf + roster_t2_offsets[t] + p * ROSTER_T2_STRIDE;
            const char *name = src[t][p];
            for (int c = 0; c < ROSTER_NAME_LEN; c++) {
                char ch = *name ? *name++ : ' ';
                if (ch >= 'A' && ch <= 'Z') {
                    d1[c] = (uint8_t)(ch - 'A');
                    d2[c] = (uint8_t)(0x80 + (ch - 'A'));
                } else if (ch >= 'a' && ch <= 'z') {
                    d1[c] = (uint8_t)(0x20 + (ch - 'a'));
                    d2[c] = (uint8_t)(0x40 + (ch - 'a'));
                } else {
                    d1[c] = 0x7F;
                    d2[c] = 0x00;
                }
            }
        }
    }
}

#endif
