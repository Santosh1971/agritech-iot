// On-screen text: scenes are written in Hindi and wrap every string in t().
// The explainer compositions call setLang("en") to show the English version.
type Lang = "hi" | "en";
let LANG: Lang = "hi";
let CONTACT: string | null = null;
export const setLang = (l: Lang) => {
  LANG = l;
};
export const setContact = (c: string | null) => {
  CONTACT = c;
};
const CONTACT_KEY = "[डीलर का नाम] · [फोन]";

const EN: Record<string, string> = {
  "(नर्सरी में Wi-Fi हो तो)": "(where the nursery has Wi-Fi)",
  "50 लीटर पूरे": "50 litres done",
  "50 लीटर ✓": "50 litres ✓",
  "FG1 फ्लोगार्ड": "FG1 FlowGuard",
  "WPC मास्टर": "WPC Master",
  "WPC लगाइए।": "Get WPC.",
  "अपने-आप OFF": "turns OFF by itself",
  "अपने-आप ON": "turns ON by itself",
  "अपने-आप चालू": "Starts by itself",
  "अपने-आप बंद ✓": "Stops by itself ✓",
  "अब आसान।": "Now it's easy.",
  "अब नहीं।": "Not any more.",
  "आज का पानी": "Today's water",
  "आप चैन से सोइए": "You sleep in peace",
  "इंटरनेट न हो तब भी चले": "Works even without internet",
  "एक दिन भूले — पौध मुरझाई": "Forget one day — seedlings wilt",
  "एक मास्टर से 20 पंप तक": "Up to 20 pumps from one master",
  "कहीं से भी फोन पर": "On your phone, from anywhere",
  "चालू करें": "Start",
  "छोटे खेत और नर्सरी के लिए": "For small farms and nurseries",
  "ज़्यादा पानी — बर्बाद": "Too much water — wasted",
  "टंकी ओवरफ्लो": "Tank overflows",
  "टंकी का लेवल": "Tank level",
  "टंकी का लेवल कम?": "Tank level low?",
  "टंकी भर गई": "Tank is full",
  "टंकी": "Tank",
  "टंकी FULL ✓": "Tank FULL ✓",
  "दिन और रात, अपने-आप": "Day and night, by itself",
  "दिन में 4 बार तक पानी": "Up to 4 waterings a day",
  "नींद खराब": "Lost sleep",
  "पंप": "Pump",
  "पानी ✓": "Water ✓",
  "पूरा ✓": "Done ✓",
  "फिर खेत जाना?": "Off to the field again?",
  "फोन पर पूरा स्टेटस": "Full status on your phone",
  "फ्लोगार्ड — स्मार्ट पानी टाइमर": "FlowGuard — smart water timer",
  "बंद करें": "Stop",
  "बाकी": "Pending",
  "बाल्टी से पानी?": "Watering by bucket?",
  "बाहर गए? पानी कौन देगा?": "Away? Who will water?",
  "बिजली गई? लौटते ही पानी फिर शुरू": "Power cut? Watering resumes when it returns",
  "बिजली ✓": "Power ✓",
  "बिना तार · लगाना आसान": "No cables · easy to install",
  "मज़दूरी का खर्च": "Labour cost",
  "मास्टर": "Master",
  "रात 2 बजे बिजली आई…": "Power comes at 2 AM…",
  "रात 3:15": "3:15 AM",
  "रोज़ की परेशानी": "The daily trouble",
  "रोज़ सुबह-शाम…": "Every morning and evening…",
  "लीटर गिनकर पानी": "Waters by the litre",
  "वायरलेस पंप कंट्रोलर": "Wireless Pump Controller",
  "शाम 5:30 · 20 मिनट": "5:30 PM · 20 min",
  "सुबह 6:00 · 50 लीटर": "6:00 AM · 50 litres",
  "सुबह 6:00 बजे —": "6:00 AM —",
  "हर रात की परेशानी": "Every night's trouble",
  "हर लीटर गिनता है": "Counts every litre",
  "● ऑनलाइन": "● Online",
  "भर रही है": "filling",
  "लीटर": "litres",
  // How it works (explainers)
  "यह कैसे काम करता है": "How it works",
  "मास्टर सम्प पढ़ता है": "Master reads the sump",
  "3 लेवल तक फ्लोट स्विच": "Float switches at up to 3 levels",
  "पंप रेडियो से चालू/बंद": "Pumps switch by radio",
  "LoRa से, पंप के मौजूदा स्टार्टर के ज़रिए": "Over LoRa, through the pump's own starter",
  "फोन पर सब दिखता है": "See it all on the phone",
  "हर पंप, बिजली और पानी का फ्लो": "Every pump, power and water flow",
  "शेड्यूल सेट करें": "Set the schedule",
  "दिन में 4 बार तक, लीटर या मिनट से": "Up to 4 a day, by litres or minutes",
  "समय पर पानी": "Waters on time",
  "इंटरनेट के बिना भी": "Even without internet",
  "फ्लो से पक्का": "Flow confirms it",
  "हर लीटर गिना जाता है": "Every litre is counted",
};

export const t = (hi: string): string => {
  if (hi === CONTACT_KEY && CONTACT) return CONTACT;
  // Explainers (contact set) show our company instead of the distributor line.
  if (hi === "NB Agri Automation") return CONTACT ? "Agri Sensors and Controls" : hi;
  return LANG === "en" ? EN[hi] ?? hi : hi;
};
