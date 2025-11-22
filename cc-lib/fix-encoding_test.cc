
#include "fix-encoding.h"

#include <string>
#include <string_view>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

#include "chardata.h"
#include "hexdump.h"
#include "text-codec.h"
#include "utf8.h"

#define CHECK_SEQ(a, b) do {                          \
    auto aa = (a);                                    \
    auto bb = (b);                                    \
    CHECK_EQ(aa, bb) << #a << "\nwhich is:\n" <<      \
      aa << "\n" << HexDump::Color(aa) <<             \
      "\nvs\n" << #b << "\nwhich is:\n" << bb <<      \
      "\n" << HexDump::Color(bb) << "\n";             \
  } while (false)

#define ALREADY_GOOD(str) do {                       \
    std::string s = (str);                           \
    std::string fixed = FixEncoding::Fix(s);         \
    CHECK(s == fixed) << "Expected the string [" <<  \
      s << "] (" << #str << ") to be unchanged " <<  \
      "by Fix. Got:\n[" << fixed << "]\n";           \
  } while (false)

#define FIX_TO(str, exp) do {                        \
    CHECK_SEQ(FixEncoding::Fix(str), exp);           \
  } while (false)

static void TestAlreadyGood() {
  ALREADY_GOOD("");
  ALREADY_GOOD("*");
  // Katakana Letter Small Tu
  ALREADY_GOOD(UTF8::Encode(0x303C));
  ALREADY_GOOD(UTF8::Encode(0x1F34C));
  ALREADY_GOOD("𝕋𝕠𝕞 𝟟");
  ALREADY_GOOD("(っ◔◡◔)っ");
  ALREADY_GOOD("Seven Bridges of Königsberg");
  ALREADY_GOOD("¿Qué?");
  ALREADY_GOOD("A ⋁ ¬A");


  // from ftfy negative.json
  ALREADY_GOOD("4288×…");
  ALREADY_GOOD("HUHLL Õ…");
  ALREADY_GOOD("RETWEET SE VOCÊ…");
  ALREADY_GOOD("PARCE QUE SUR LEURS PLAQUES IL Y MARQUÉ…");
  ALREADY_GOOD("TEM QUE SEGUIR, SDV SÓ…");

  // These are using accents as quotes. Maybe a future version should
  // normalize those?
  ALREADY_GOOD("Η ¨ανατροφή¨ δυστυχώς από τους προπονητές");
  ALREADY_GOOD("``toda produzida pronta pra assa aí´´");
  ALREADY_GOOD("Ôôô VIDA MINHA");
  // Without quote uncurling.
  ALREADY_GOOD("Join ZZAJÉ’s Official Fan List and receive news, events, and more!");
  ALREADY_GOOD("[x]\u00a0©");
  ALREADY_GOOD("2012—∞");
  ALREADY_GOOD("SENSЕ - Oleg Tsedryk");
  ALREADY_GOOD("OK??:(   `¬´    ):");
  ALREADY_GOOD("( o¬ô )");
  ALREADY_GOOD("∆°");
  ALREADY_GOOD("ESSE CARA AI QUEM É¿");
  ALREADY_GOOD("SELKÄ\u00a0EDELLÄ\u00a0MAAHAN via @YouTube");
  ALREADY_GOOD("Offering 5×£35 pin ups");
  ALREADY_GOOD("NESTLÉ® requiere");
  ALREADY_GOOD("C O N C L U S Ã O");
  ALREADY_GOOD("Oborzos, per. Vahbarz, frataraká§ 141");
  ALREADY_GOOD("(-1/2)! = √π");
  ALREADY_GOOD("MÄ£ÄM ÌÑÌ Q £ÄGÌ GÄLÄW ÑÍCH SÖÄ£ ÑÝÄ $ÚÄMÌ Q £ÄGÌ GÄK "
               "ÉÑÄK BÄDÄÑ....?????????,                     ......JÄDÍ...");
  ALREADY_GOOD("├┤a┼┐a┼┐a┼┐a┼┐a");
  // A-with-circle as an Angstrom sign
  // Should not turn into '10 ŗ'
  ALREADY_GOOD("a radius of 10 Å—");
  ALREADY_GOOD("!YO SÉ¡");
}

static void TestFtfySynthetic() {
  FIX_TO("C'est vrai que nous n'en avons pas encore beaucoup parlé\u0085 "
         "Tu sais, ça fait de nombreuses années",
         "C'est vrai que nous n'en avons pas encore beaucoup parlé… "
         "Tu sais, ça fait de nombreuses années");

  // we can recognize Ã at the end of a word when it absorbs a following space
  FIX_TO("voilÃ le travail",
         "voilà le travail");
  // we can recognize Ã in some cases when it's the only mojibake
  FIX_TO("voilÃ  le travail",
         "voilà le travail");
  // Hebrew UTF-8 / Windows-1250 mojibake
  FIX_TO("×‘×”×•×“×˘×”",
         "בהודעה");
  // Hebrew UTF-8 / MacRoman mojibake
  FIX_TO("◊ë◊î◊ï◊ì◊¢◊î",
         "בהודעה");
  // Hebrew UTF-8 / Latin-1 mojibake
  FIX_TO("×\u0090×\u0091×\u0091×\u0090",
         "אבבא");
  // Arabic UTF-8 / Windows-1252 mojibake
  FIX_TO("Ø±Ø³Ø§Ù„Ø©",
         "رسالة");
  // Arabic UTF-8 / Windows-1250 mojibake
  FIX_TO("Ř±ŘłŘ§Ů„Ř©",
         "رسالة");
  // Arabic UTF-8 / MacRoman mojibake
  FIX_TO("ÿ±ÿ≥ÿßŸÑÿ©",
         "رسالة");
  // Brontë's name does not end with a Korean syllable
  // The original example of why ftfy needs heuristics
  FIX_TO("I'm not such a fan of Charlotte Brontë…”",
         "I'm not such a fan of Charlotte Brontë…”");
  FIX_TO("AHÅ™, the new sofa from IKEA",
         "AHÅ™, the new sofa from IKEA");
  // Ukrainian capital letters
  // We need to fix Windows-1251 conservatively, or else this decodes as '²ʲ'
  FIX_TO("ВІКІ is Ukrainian for WIKI",
         "ВІКІ is Ukrainian for WIKI");
  // We use byte 0x1A internally as an encoding of U+FFFD, but literal
  // occurrences of U+1A are just ASCII control characters
  FIX_TO("These control characters \u001a are apparently intentional \u0081",
         "These control characters  are apparently intentional \u0081");
  FIX_TO("Here's a control character: \u001a",
         "Here's a control character: ");

  // fix text with backslashes in it
  // Tests for a regression on a long-ago bug
  FIX_TO("<40\\% vs \u00e2\u0089\u00a540\\%",
         "<40\\% vs ≥40\\%");
  // curly quotes with mismatched encoding glitches in Latin-1
  FIX_TO("\u00e2\u0080\u009cmismatched quotes\u0085\u0094",
         "“mismatched quotes…”");
  // curly quotes with mismatched encoding glitches in Windows-1252
  FIX_TO("â€œmismatched quotesâ€¦”",
         "“mismatched quotes…”");
  // lossy decoding in sloppy-windows-1252
  FIX_TO("â€œlossy decodingâ€�",
         "“lossy decoding�");
  // French word for August in windows-1252
  FIX_TO("aoÃ»t",
         "août");
  // French word for hotel in all-caps windows-1252
  FIX_TO("HÃ”TEL",
         "HÔTEL");
  // Scottish Gaelic word for 'subject' in all-caps windows-1252
  FIX_TO("CÃ™IS",
         "CÙIS");
  // Synthetic, negative: Romanian word before a non-breaking space
  FIX_TO("NICIODATĂ\u00a0",
         "NICIODATĂ\u00a0");
  // Synthetic, negative: Be careful around curly apostrophes
  // It shouldn't end up saying 'a lot of Òs'
  FIX_TO("There are a lot of Ã’s in mojibake text",
         "There are a lot of Ã’s in mojibake text");
  // Synthetic, negative: Romanian word before a trademark sign
  // We would change 'DATÃ™' to 'DATÙ' if it passed the badness heuristic
  FIX_TO("NICIODATĂ™",
         "NICIODATĂ™");
  // Synthetic, negative: Lithuanian word before a trademark sign
  // Similar to the above example. Shouldn't turn into U+0619 ARABIC SMALL DAMMA
  FIX_TO("TRANSFORMATORIŲ™",
         "TRANSFORMATORIŲ™");
  // Synthetic, negative: Norwegian capitalized nonsense
  FIX_TO("HÅØYA ER BLÅØYD",
         "HÅØYA ER BLÅØYD");
  // Synthetic, negative: raised eyebrow kaomoji
  FIX_TO("Ō¬o",
         "Ō¬o");
  // Synthetic, negative: Camel-cased Serbian that looks like a UTF-8 / Windows-1251 mixup
  FIX_TO("ПоздравЂаво",
         "ПоздравЂаво");
  // mojibake with trademark sign at the end of a word
  FIX_TO("OÃ™ ET QUAND?",
         "OÙ ET QUAND?");
}

static void TestFtfyInTheWild() {
  // Low-codepoint emoji
  // From the ancient era before widespread emoji support on Twitter
  FIX_TO("He's Justinâ\u009d¤",
         "He's Justin❤");
  // UTF-8 / MacRoman mix-up about smurfs
  FIX_TO("Le Schtroumpf Docteur conseille g√¢teaux et baies schtroumpfantes pour un r√©gime √©quilibr√©.",
         "Le Schtroumpf Docteur conseille gâteaux et baies schtroumpfantes pour un régime équilibré.");
  // Checkmark that almost looks okay as mojibake
  FIX_TO("âœ” No problems",
         "✔ No problems");
  // UTF-8 / Windows-1251 Russian mixup about futbol
  FIX_TO("РґРѕСЂРѕРіРµ Р\u0098Р·-РїРѕРґ #С„СѓС‚Р±РѕР»",
         "дороге Из-под #футбол");
  // Latin-1 / Windows-1252 mixup in German
  FIX_TO("\u0084Handwerk bringt dich überall hin\u0093: Von der YOU bis nach Monaco",
         "„Handwerk bringt dich überall hin“: Von der YOU bis nach Monaco");
  // Latin-1 / Windows-1252 mixup of the replacement character
  FIX_TO("Some comments may be republished on the website or in the newspaper ï¿½ email addresses will not be published.",
         "Some comments may be republished on the website or in the newspaper � email addresses will not be published.");
  // CESU-8 / Windows-1252 emoji
  FIX_TO("Hi guys í\u00a0½í¸\u008d",
         "Hi guys 😍");
  // CESU-8 / Latin-1 emoji
  FIX_TO("hihi RT username: â\u0098ºí\u00a0½í¸\u0098",
         "hihi RT username: ☺😘");
  // Latin-1 / Windows-1252 mixup in Turkish
  FIX_TO("Beta Haber: HÄ±rsÄ±zÄ± BÃ¼yÃ¼ Korkuttu",
         "Beta Haber: Hırsızı Büyü Korkuttu");
  // Latin-1 / Windows-1252 mixup in İstanbul (issue #192)
  FIX_TO("Ä°stanbul",
         "İstanbul");
  // Latin-1 / Windows-1252 mixup in German (issue #188)
  FIX_TO("RUF MICH ZURÃœCK",
         "RUF MICH ZURÜCK");
  // Latin-1 / Windows-1252 mixup in Rīga (issue #192)
  FIX_TO("RÄ«ga",
         "Rīga");
  // UTF-8 / Windows-1251 mixed up twice in Russian
  FIX_TO("Р\u00a0С—РЎР‚Р\u00a0С‘РЎРЏРЎвЂљР\u00a0Р…Р\u00a0С•РЎРѓРЎвЂљР\u00a0С‘. РІСњВ¤",
         "приятности. ❤");
  // UTF-8 / Windows-1252 mixed up twice in Malay
  FIX_TO("Kayanya laptopku error deh, soalnya tiap mau ngetik deket-deket kamu font yg keluar selalu Times New Ã¢â‚¬Å“ RomanceÃ¢â‚¬Â\u009d.",
         "Kayanya laptopku error deh, soalnya tiap mau ngetik deket-deket kamu font yg keluar selalu Times New “ Romance”.");
  // UTF-8 / Windows-1252 mixed up twice in naming Iggy Pop
  FIX_TO("Iggy Pop (nÃƒÂ© Jim Osterberg)",
         "Iggy Pop (né Jim Osterberg)");
  // Left quote is UTF-8, right quote is Latin-1, both encoded in Windows-1252
  FIX_TO("Direzione Pd, ok â\u0080\u009csenza modifiche\u0094 all'Italicum.",
         "Direzione Pd, ok “senza modifiche” all'Italicum.");
  // UTF-8 / sloppy Windows-1252 mixed up twice in a triumphant emoticon
  FIX_TO("selamat berpuasa sob (Ã\u00a0Â¸â€¡'ÃŒâ‚¬Ã¢Å’Â£'ÃŒÂ\u0081)Ã\u00a0Â¸â€¡",
         "selamat berpuasa sob (ง'̀⌣'́)ง");
  // UTF-8 / Windows-1252 mixed up three times
  FIX_TO("The Mona Lisa doesnÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢t have eyebrows.",
         "The Mona Lisa doesn’t have eyebrows.");
  // UTF-8 / Codepage 437 mixup in Russian
  FIX_TO("#╨┐╤Ç╨░╨▓╨╕╨╗╤î╨╜╨╛╨╡╨┐╨╕╤é╨░╨╜╨╕╨╡",
         "#правильноепитание");
  // UTF-8 / Windows-1252 mixup in French
  FIX_TO("HÃ´tel de Police",
         "Hôtel de Police");
  // UTF-8 / Windows-1250 mixup in French
  FIX_TO("LiĂ¨ge Avenue de l'HĂ´pital",
         "Liège Avenue de l'Hôpital");
  // UTF-8 / Windows-1252 mixup in Vietnamese
  FIX_TO("Táº¡i sao giÃ¡ háº¡t sáº§u riÃªng láº¡i lÃªn giÃ¡?",
         "Tại sao giá hạt sầu riêng lại lên giá?");
  // Science! Mid-word Greek letter gets fixed correctly
  FIX_TO("Humanized HLA-DR4.RagKO.IL2RÎ³cKO.NOD (DRAG) mice sustain the complex vertebrate life cycle of Plasmodium falciparum malaria.",
         "Humanized HLA-DR4.RagKO.IL2RγcKO.NOD (DRAG) mice sustain the complex vertebrate life cycle of Plasmodium falciparum malaria.");
  // For goodness' sake. We can come close to fixing this, but fail in the last step
  FIX_TO("ItÃ?Â¢â?¬â?¢s classic. ItÃ?Â¢â?¬â?¢s epic. ItÃ?Â¢â?¬â?¢s ELIZABETH BENNET for goodnessÃ?Â¢â?¬â?¢ sake!",
         "It�¢��s classic. It�¢��s epic. It�¢��s ELIZABETH BENNET for goodness�¢�� sake!");
  // lossy UTF-8 / Windows-1250 mixup in Spanish
  FIX_TO("Europa, Asia, Ă�frica, Norte, AmĂ©rica Central y del Sur, Australia y OceanĂ­a",
         "Europa, Asia, �frica, Norte, América Central y del Sur, Australia y Oceanía");
  // UTF-8 / sloppy Windows-1250 mixup in English
  FIX_TO("It was namedÂ â€žscarsÂ´ stonesâ€ś after the rock-climbers who got hurt while climbing on it.",
         "It was named\u00a0„scars´ stones“ after the rock-climbers who got hurt while climbing on it.");
  // The same text as above, but as a UTF-8 / ISO-8859-2 mixup
  FIX_TO("It was namedÂ\u00a0â\u0080\u009escarsÂ´ stonesâ\u0080\u009c after the rock-climbers who got hurt while climbing on it.",
         "It was named\u00a0„scars´ stones“ after the rock-climbers who got hurt while climbing on it.");
  // UTF-8 / ISO-8859-2 mixup in Czech
  // This says 'I've had enough of the third millennium', which is great because it involves software decisions made in the second
  FIX_TO("MĂĄm dost tĹ\u0099etĂ\u00adho tisĂ\u00adciletĂ\u00ad",
         "Mám dost třetího tisíciletí");
  // UTF-8 / Windows-1252 mixup in mixed French and Arabic
  // A difficult test case that can depend on the order that steps are applied
  FIX_TO("Ã€ tous mes frÃ¨res et soeurs dans la syriennetÃ© comme dans l’humanitÃ©, sans discrimination aucune, je vous souhaite bonne fÃªte Ø¹ÙŠØ¯ Ø³Ø¹ÙŠØ¯.Que la paix, la libertÃ©, l’Ã©galitÃ©, la fraternitÃ© et la dignitÃ© soient avec vous.Pardonnez ce ton un peu ecclÃ©siastique.",
         "À tous mes frères et soeurs dans la syrienneté comme dans l’humanité, sans discrimination aucune, je vous souhaite bonne fête عيد سعيد.Que la paix, la liberté, l’égalité, la fraternité et la dignité soient avec vous.Pardonnez ce ton un peu ecclésiastique.");
  // UTF-8 / sloppy Windows-1250 mixup in Romanian
  FIX_TO("vedere Ă®nceĹŁoĹźatÄ\u0083",
         "vedere înceţoşată");
  // UTF-8 / Windows-1250 mixup in Slovak
  FIX_TO("NapĂ\u00adĹˇte nĂˇm !",
         "Napíšte nám !");
  // UTF-8 / Windows-1252 mixup in Spanish
  FIX_TO("DOS AÃ‘OS",
         "DOS AÑOS");
  // UTF-8 / Windows-1252 followed by UTF-8 / Windows-1251
  FIX_TO("a bigger-than-expected Г‚ВЈ5.8bn rights issue to satisfy ",
         "a bigger-than-expected £5.8bn rights issue to satisfy ");
  // fancy Unicode crossing-out, but mojibaked
  FIX_TO("hotel $49 $Ì¶6Ì¶3Ì¶ updated 2018",
         "hotel $49 $̶6̶3̶ updated 2018");
  // A face with UTF-8 / sloppy Windows-1252 mixed up twice
  FIX_TO("Ã¢â€\u009dâ€™(Ã¢Å’Â£Ã‹â€ºÃ¢Å’Â£)Ã¢â€\u009dÅ½",
         "┒(⌣˛⌣)┎");
  // We can mostly decode the face above when we lose the character U+009D
  FIX_TO("Ã¢â€�â€™(Ã¢Å’Â£Ã‹â€ºÃ¢Å’Â£)Ã¢â€�Å½",
         "�(⌣˛⌣)�");
  // Lossy decoding can have plain ASCII question marks, as well
  FIX_TO("The ICR has been upgraded to â€œbb+â€? from â€œbbâ€?",
         "The ICR has been upgraded to “bb+� from “bb�");
  // CESU-8 / Latin-1 mixup over several emoji
  FIX_TO("I just figured out how to tweet emojis! â\u009a½í\u00a0½í¸\u0080í\u00a0½í¸\u0081í\u00a0½í¸\u0082í\u00a0½í¸\u0086í\u00a0½í¸\u008eí\u00a0½í¸\u008eí\u00a0½í¸\u008eí\u00a0½í¸\u008e",
         "I just figured out how to tweet emojis! ⚽😀😁😂😆😎😎😎😎");
  // Inconsistent UTF-8 / Latin-1 mojibake
  FIX_TO("Ecuadorâ\u0080\u0099s â\u0080\u0098purely political decision on Assangeâ\u0080\u0099 is likely result of â\u0080\u0098US pressureâ\u0080\u0099\u0085",
         "Ecuador’s ‘purely political decision on Assange’ is likely result of ‘US pressure’…");
  // Inconsistent UTF-8 / Latin-1 mojibake with an ellipsis from
  // the Windows-1252 character set
  FIX_TO("Ecuadorâ\u0080\u0099s â\u0080\u0098purely political decision on Assangeâ\u0080\u0099 is likely result of â\u0080\u0098US pressureâ\u0080\u0099…",
         "Ecuador’s ‘purely political decision on Assange’ is likely result of ‘US pressure’…");
  // Inconsistent mojibake in Portuguese
  FIX_TO("Campeonatos > III DivisÃ£o - SÃ©rie F > Jornadas Classificação",
         "Campeonatos > III Divisão - Série F > Jornadas Classificação");
  // Handle Afrikaans 'n character
  FIX_TO("ŉ Chloroplas is ŉ organel wat in fotosinterende plante voorkom.",
         "ʼn Chloroplas is ʼn organel wat in fotosinterende plante voorkom.");
  // Handle Croatian single-codepoint digraphs
  FIX_TO("izum „bootstrap load“ koji je korišteǌem polisilicijskog sloja proizveo dovoǉno dobre kondenzatore na čipu",
         "izum „bootstrap load“ koji je korištenjem polisilicijskog sloja proizveo dovoljno dobre kondenzatore na čipu");
  // A with an acute accent, in isolation
  FIX_TO("NicolÃ¡s",
         "Nicolás");
  // sharp S, in isolation, via MacRoman encoding
  // regression reported in issue #186
  FIX_TO("wei√ü",
         "weiß");
  // French example containing non-breaking spaces
  FIX_TO("ART TRIP Ã\u00a0 l'office de tourisme",
         "ART TRIP à l'office de tourisme");
  // English example in UTF-8 / Windows-1251 with a ligature
  FIX_TO("This is signiп¬Ѓcantly lower than the respective share",
         "This is significantly lower than the respective share");
  // 'à' remains its own word, even if spaces after it get coalesced into one
  FIX_TO("Ã perturber la rÃ©flexion des thÃ©ologiens jusqu'Ã nos jours",
         "à perturber la réflexion des théologiens jusqu'à nos jours");
  // Fix 'à' in inconsistent mojibake
  FIX_TO("Le barÃ¨me forfaitaire permet l’Ã©valuation des frais de dÃ©placement relatifs Ã l’utilisation",
         "Le barème forfaitaire permet l’évaluation des frais de déplacement relatifs à l’utilisation");
  // The Portuguese word 'às' does not become 'à s' due to the French fix
  FIX_TO("com especial atenÃ§Ã£o Ã s crianÃ§as",
         "com especial atenção às crianças");
  // This is why we require a space after the 's' in 'às'
  FIX_TO("TroisiÃ¨me Ã©dition pour ce festival qui persiste et signe Ã s'Ã©loigner des grands axes pour prendre les contre-allÃ©es en 16 concerts dans 7 villes de 2 pays voisins.",
         "Troisième édition pour ce festival qui persiste et signe à s'éloigner des grands axes pour prendre les contre-allées en 16 concerts dans 7 villes de 2 pays voisins.");
  // We can fix 'à' in windows-1251 sometimes as well
  FIX_TO("La rГ©gion de Dnepropetrovsk se trouve Г lвЂ™ouest de lвЂ™Ukraine",
         "La région de Dnepropetrovsk se trouve à l’ouest de l’Ukraine");
  // 'Ã quele' is the Portuguese word 'àquele', not 'à quele'
  FIX_TO("eliminado o antÃ­geno e mantidos os nÃ­veis de anticorpos, surgem as condiÃ§Ãµes necessÃ¡rias ao estabelecimento do granuloma, semelhante Ã quele observado nas lesÃµes por imunocomplexo em excesso de anticorpos",
         "eliminado o antígeno e mantidos os níveis de anticorpos, surgem as condições necessárias ao estabelecimento do granuloma, semelhante àquele observado nas lesões por imunocomplexo em excesso de anticorpos");
  // A complex, lossy pile-up of mojibake in Portuguese
  FIX_TO("â € ðŸ“�Â Regulamento: â € âš ï¸� As pessoas que marcarem nos comentÃ¡rios perfis empresariais e/ou de marcas, personalidades ou fake serÃ£o desclassificadas. âš ï¸� Podem participar pessoas residentes em Petrolina/PE ou Juazeiro/BA, desde que se comprometam a retirar o prÃªmio em nosso endereÃ§o. FuncionÃ¡rios estÃ£o vetados. âš ï¸� SerÃ£o vÃ¡lidos os comentÃ¡rios postados atÃ© 16h, do dia 31/03/2018. E o resultado serÃ¡ divulgado atÃ© Ã s 19h do mesmo dia em uma nova publicaÃ§Ã£o em nosso instagram. â € Boa sorte!!!Â ðŸ˜€ðŸ�°",
         "⠀ �\u00a0Regulamento: ⠀ ⚠� As pessoas que marcarem nos comentários perfis empresariais e/ou de marcas, personalidades ou fake serão desclassificadas. ⚠� Podem participar pessoas residentes em Petrolina/PE ou Juazeiro/BA, desde que se comprometam a retirar o prêmio em nosso endereço. Funcionários estão vetados. ⚠� Serão válidos os comentários postados até 16h, do dia 31/03/2018. E o resultado será divulgado até às 19h do mesmo dia em uma nova publicação em nosso instagram. ⠀ Boa sorte!!!\u00a0😀�");
  // UTF-8 / Windows-1252 mixup in Gaelic involving non-breaking spaces
  FIX_TO("CÃ\u00a0nan nan GÃ\u00a0idheal",
         "Cànan nan Gàidheal");
  // UTF-8 / Windows-1251 mixup in tweet spam
  FIX_TO("Blog Traffic Tip 2 вЂ“ Broadcast Email Your Blog",
         "Blog Traffic Tip 2 – Broadcast Email Your Blog");
  // UTF-8 / Windows-1251 mixup
  FIX_TO("S&P Confirms UkrsotsbankвЂ™s вЂњB-вЂњ Rating",
         "S&P Confirms Ukrsotsbank’s “B-“ Rating");
  // Dutch example with ë
  // from issue reported by MicroJackson
  FIX_TO("ongeÃ«venaard",
         "ongeëvenaard");
  // Three layers of UTF-8 / MacRoman mixup in French
  // You're welcome
  FIX_TO("Merci de t‚Äö√†√∂¬¨¬©l‚Äö√†√∂¬¨¬©charger le plug-in Flash Player 8",
         "Merci de télécharger le plug-in Flash Player 8");
  // UTF-8 / MacRoman mixup in French
  FIX_TO("Merci de bien vouloir activiter le Javascript dans votre navigateur web afin d'en profiter‚Ä¶",
         "Merci de bien vouloir activiter le Javascript dans votre navigateur web afin d'en profiter…");
  // Italian UTF-8 / MacRoman example with ò
  FIX_TO("Le Vigne di Zam√≤",
         "Le Vigne di Zamò");
  // Punctuation pile-up should actually be musical notes
  FIX_TO("Engkau masih yg terindah, indah di dalam hatikuâ™«~",
         "Engkau masih yg terindah, indah di dalam hatiku♫~");
  // Latvian UTF-8 / Windows-1257 mojibake
  FIX_TO("Å veices baÅ†Ä·ieri gaida konkrÄ“tus investÄ«ciju projektus",
         "Šveices baņķieri gaida konkrētus investīciju projektus");
  // Latvian UTF-8 / MacRoman mojibake
  FIX_TO("SaeimƒÅ ievƒìlƒìtƒÅs partijas \"Progresƒ´vie\" lƒ´dzvadƒ´tƒÅja Anto≈Üina ≈Öena≈°eva atbild uz ≈æurnƒÅlistu jautƒÅjumiem pƒìc partijas tik≈°anƒÅs ar Valsts prezidentu Rƒ´gas pilƒ´,",
         "Saeimā ievēlētās partijas \"Progresīvie\" līdzvadītāja Antoņina Ņenaševa atbild uz žurnālistu jautājumiem pēc partijas tikšanās ar Valsts prezidentu Rīgas pilī,");
  // Lithuanian UTF-8 / Windows-1257 mojibake
  FIX_TO("Å iaip ÄÆdomu, kaip ÄÆsivaizduoji. VisÅ³ pirma tam reikia laiko.",
         "Šiaip įdomu, kaip įsivaizduoji. Visų pirma tam reikia laiko.");
  // Lithuanian UTF-8 / Windows-1250 mojibake
  FIX_TO("Lietuva pagrÄŻstai gali paklausti: Ĺ˝inoma, kad ne.",
         "Lietuva pagrįstai gali paklausti: Žinoma, kad ne.");
  // Hebrew UTF-8 / Windows-1252 mojibake
  // reported by SuperIRabbit as issue #158
  FIX_TO("×‘×”×•×“×¢×”",
         "בהודעה");
  // Wide comma in UTF-8 / Windows-1252
  FIX_TO("Ningboï¼ŒChina",
         "Ningbo\uff0cChina");
}

static void ExpectedNotBad() {
  CHECK(!FixEncoding::IsBad("Más café, por favor."));
  CHECK(!FixEncoding::IsBad("Voilà"));
  CHECK(!FixEncoding::IsBad("100°C"));
  CHECK(!FixEncoding::IsBad("£500"));
  CHECK(!FixEncoding::IsBad("Ελλάδα"));
  CHECK(!FixEncoding::IsBad("Россия"));
}

static void ExpectedBad() {
  // Pattern: Ã[\u00a0¡]
  // This is mojibake for "à" (C3 A0) -> "Ã" + NBSP.
  // Makes sure that we are using the UTF-8 encoding of U+00A0, not the byte \xa0.
  CHECK(FixEncoding::IsBad("Ã\u00a0"));

  // Common Windows-1252 2-char mojibake
  // Mojibake for "é" (C3 A9) -> "Ã" + "©"
  CHECK(FixEncoding::IsBad("Ã©"));

  // Mojibake for "í" (C3 AD) -> "Ã" + Soft Hyphen
  CHECK(FixEncoding::IsBad("Ã\u00ad"));

  // C1 Control Characters
  // These are almost never intended in valid text.
  CHECK(FixEncoding::IsBad("Test\u0080Case"));

  // "â" (Lower Accented) + "–" (Box/Punctuation range)
  CHECK(FixEncoding::IsBad("â│"));

  // Windows-1252 encodings of 'à' and 'á' with context
  // Mojibake for "fácil" -> "fÃ cil"
  CHECK(FixEncoding::IsBad("fÃ cil"));

  CHECK(FixEncoding::IsBad("β€®"));

  // "â‚¬" is the UTF-8 encoding of €, interpreted as Windows-1252.
  CHECK(FixEncoding::IsBad("â‚¬"));

  CHECK(FixEncoding::IsBad("Ã§"));

  // Make sure that regex ranges [a-b] between UTF-8-encoded codepoints
  // work correctly.
  CHECK(FixEncoding::IsBad("│Õ"));
  CHECK(FixEncoding::IsBad("xλ¬"));
}

static void TestVariantDecode() {
  auto MustDecode = [](std::string_view str) {
      auto v = FixEncoding::DecodeVariantUTF8(str);
      CHECK(v.has_value()) << "Failed to decode: " << str;
      return v.value();
    };

  CHECK_SEQ(MustDecode("\xC0\x80"), std::string("\0", 1));
  CHECK_SEQ(MustDecode("null \xC0\x80!"), std::string("null \0!", 7));

  // Surrogate pair.
  CHECK_SEQ(MustDecode(
               // U+D83D
               "\xED\xA0\xBD"
               // U+DCA9
               "\xED\xB2\xA9"), "💩");
  CHECK_SEQ(MustDecode("PO" "\xED\xA0\xBD\xED\xB2\xA9" "P"),
            "PO💩P");

  // Incomplete surrogate pairs
  CHECK_SEQ(MustDecode("\xED\xA0\xBD"), "\xED\xA0\xBD");
  CHECK_SEQ(MustDecode("\xED\xA0\xBDx"), "\xED\xA0\xBDx");
  CHECK_SEQ(MustDecode("\xED\xA0\xBD\xED\xA0\xBD"),
            "\xED\xA0\xBD\xED\xA0\xBD");

  CHECK(!FixEncoding::DecodeVariantUTF8("\xC0").has_value());
  CHECK(!FixEncoding::DecodeVariantUTF8("\xC0\x81").has_value());
  CHECK(!FixEncoding::DecodeVariantUTF8("\xC0\x00").has_value());

  CHECK(!FixEncoding::DecodeVariantUTF8("\xFF").has_value());
  CHECK(!FixEncoding::DecodeVariantUTF8("\x80").has_value());
  CHECK(!FixEncoding::DecodeVariantUTF8("\xE0\x80").has_value());
  CHECK(!FixEncoding::DecodeVariantUTF8("\xED\xA0").has_value());
}

void TestTextCodecs() {
  const TextCodec &w1252 = Windows1252();
  {
    // U+20AC is Euro Sign = Windows-1252 0x80
    std::string_view euro = "€";
    auto encoded = w1252.Encode(euro);
    CHECK(encoded.has_value());
    CHECK(encoded.value().size() == 1);
    CHECK((uint8_t)encoded.value()[0] == 0x80);

    auto decoded = w1252.Decode(encoded.value());
    CHECK(decoded.has_value());
    CHECK(decoded.value() == euro);
  }

  {
    // This byte is not mapped.
    std::string_view bytes_hole = "\x81";
    auto decoded = w1252.Decode(bytes_hole);
    CHECK(!decoded.has_value());

    std::string_view cp_hole = "\u0081";
    auto encoded = w1252.Encode(cp_hole);
    CHECK(!encoded.has_value());
  }

  {
    // But in sloppy mode, it should encode/decode to itself.
    std::string_view bytes_hole = "\x81";
    std::string decoded = w1252.DecodeSloppy(bytes_hole);
    CHECK(decoded == "\u0081");

    auto encoded = w1252.EncodeSloppy("\u0081");
    CHECK(encoded.has_value());
    CHECK(encoded.value() == bytes_hole);
  }

  // ftfy has a special hack for 0x1A <=> U+FFFD.
  {
    std::string_view bytes_sub = "\x1A";
    std::string decoded = w1252.DecodeSloppy(bytes_sub);
    std::string_view replacement = "\xEF\xBF\xBD";
    CHECK(decoded == replacement);

    auto encoded = w1252.EncodeSloppy(replacement);
    CHECK(encoded.has_value());
    CHECK(encoded.value() == bytes_sub);
  }

  // Latin-1 has no holes. 0x81 -> U+0081.
  const TextCodec &latin1 = Latin1();
  {
    std::string_view cp_81 = "\u0081";
    auto encoded = latin1.Encode(cp_81);
    CHECK(encoded.has_value());
    CHECK(encoded.value() == "\x81");

    auto decoded = latin1.Decode("\x81");
    CHECK(decoded.has_value());
    CHECK(decoded.value() == cp_81);
  }

  // Ensure strict codecs don't do the 0x1A hack in their strict mode.
  const TextCodec &macroman = MacRoman();
  {
    // 0x1A in MacRoman is just U+001A (Control)
    std::string_view sub = "\x1A";
    auto decoded = macroman.Decode(sub);
    CHECK(decoded.has_value());
    CHECK(decoded.value() == "\u001A");

    auto encoded = macroman.Encode("\u001A");
    CHECK(encoded.has_value());
    CHECK(encoded.value() == sub);

    // U+FFFD should fail strict encode
    CHECK(!macroman.Encode("\xEF\xBF\xBD").has_value());
  }
}

static void TestC1Controls() {
  // € is \xE2\x82\xAC.
  // Byte \x82 is a C1 control if interpreted as a single byte;
  // check that we don't mangle it.
  CHECK(FixEncoding::FixC1Controls("€") == "€");

  // On the other hand, a C1 control character encoded properly as
  // UTF-8 should be translated to something useful.
  // Latin-1 0x80 -> Windows-1252 0x80 -> U+20AC (Euro).
  CHECK(FixEncoding::FixC1Controls("\u0080") == "€");
}

static void TestDecodeInconsistentUTF8() {
  // Ă is \xC4\x82.
  // \xC4 looks like a Windows-1252 Lead Byte (U+00C4 -> Ä).
  // \x82 looks like a Continuation Byte (in the range 0x80-0xBF).
  // Make sure we don't incorrectly interpret this perfectly
  // fine UTF-8 as mojibake.
  CHECK(DecodeInconsistentUTF8("Ă") == "Ă");
}

static void TestFixSurrogates() {
  CHECK_SEQ(FixSurrogates("*"), "*");
  CHECK_SEQ(FixSurrogates(""), "");

  // Valid Surrogate Pair: U+D83D + U+DCA9 -> U+1F4A9 (Pile of Poo)
  std::string input_pair;
  input_pair.append(UTF8::Encode(0xD83D));
  input_pair.append(UTF8::Encode(0xDCA9));
  CHECK_SEQ(FixSurrogates(input_pair), "💩");

  // Isolated High Surrogate -> Replacement Char
  CHECK_SEQ(FixSurrogates("\xED\xA0\xBD"), UTF8::Encode(UTF8::REPLACEMENT_CODEPOINT));

  // Isolated Low Surrogate -> Replacement Char
  CHECK_SEQ(FixSurrogates("\xED\xB2\xA9"), UTF8::Encode(UTF8::REPLACEMENT_CODEPOINT));

  CHECK_SEQ(FixSurrogates("a" + input_pair + "z"),"a💩z");

  // Unpaired high surrogate.
  CHECK_SEQ(FixSurrogates("\xED\xA0\xBD" "a"),
            UTF8::Encode(UTF8::REPLACEMENT_CODEPOINT) + "a");
}

static void TestRestoreByteA0() {
  // Ã normal cases (e.g. "Ã la mode" -> "à la mode").
  CHECK_SEQ(RestoreByteA0("\xC3 la"), "à la");

  // Ã exception cases (Portuguese contractions like "àquele").
  CHECK_SEQ(RestoreByteA0("\xC3 quele"), "àquele");
  CHECK_SEQ(RestoreByteA0("\xC3 s the"), "às the");

  // Non-breaking space restoration.
  CHECK_SEQ(RestoreByteA0("100\xC2 km"), "100\u00a0km");

  CHECK_SEQ(RestoreByteA0("Hello World"), "Hello World");
  // Regression.
  CHECK_SEQ(RestoreByteA0("\xC5 "), "Š");
}


int main(int argc, char **argv) {
  ANSI::Init();

  TestTextCodecs();

  TestC1Controls();
  TestDecodeInconsistentUTF8();
  TestFixSurrogates();
  TestVariantDecode();
  TestRestoreByteA0();

  TestAlreadyGood();
  ExpectedNotBad();
  ExpectedBad();

  TestFtfySynthetic();
  TestFtfyInTheWild();

  Print("OK\n");
  return 0;
}
