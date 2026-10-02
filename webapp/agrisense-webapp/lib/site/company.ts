// Company details shown on the public website. A field left null is simply
// not shown, so the site never displays a made-up phone number or address.
export const company = {
  name: "Agri Sensors and Controls",
  domain: "agrisenseandcontrol.in",
  tagline: "Sensors, controllers and apps that save farmers water, power and labour",
  email: "jha.santosh.kr@gmail.com" as string | null,
  address: null as string | null,
};

// People to call or WhatsApp; each number works for both.
export const contacts = [
  { name: "Avinash Jha", role: "CEO · sales and dealers", phone: "+91 70629 41806" },
  { name: "Santosh Kumar Jha", role: "CTO · products, colleges and partners", phone: "+91 81972 39206" },
];

export const distributors = [
  { name: "NB Agri Automation", region: "Chhattisgarh", role: "Sales, installation and service" },
];

export const team = [
  {
    name: "Avinash Jha",
    role: "CEO",
    bio: "5+ years building IoT products at Sasya Systems, Amazon India and Tinymesh. Leads customers, dealers and field deployments.",
  },
  {
    name: "Santosh Kumar Jha",
    role: "CTO",
    bio: "30+ years in embedded systems, IoT and telematics: ACTIA, General Motors, Director of the Autocop Excellence Center. MANIT Bhopal; M.Eng, University of Michigan. Designs our hardware, firmware and apps.",
  },
];
