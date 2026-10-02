// Simple line icons for the public site (no icon library dependency).
type Name =
  | "drop" | "pump" | "valve" | "soil" | "leaf" | "snow" | "fish" | "clock"
  | "moon" | "bolt" | "school" | "chip" | "phone" | "users";

const paths: Record<Name, React.ReactNode> = {
  drop: <path d="M12 3c-3 4-6 7.5-6 11a6 6 0 0 0 12 0c0-3.5-3-7-6-11z" />,
  pump: <><rect x="4" y="9" width="10" height="9" rx="2" /><path d="M14 12h4v-3h2M9 9V5h6" /></>,
  valve: <><path d="M3 14h6m6 0h6M9 10v8M15 10v8" /><path d="M9 14l6-3v6z" /><path d="M12 11V6m-3 0h6" /></>,
  soil: <><path d="M3 10h18" /><path d="M12 10V4m0 0c-2 0-3.5 1-4 2.5M12 4c2 0 3.5 1 4 2.5" /><path d="M6 14h.01M11 16h.01M16 14h.01M8 19h.01M14 19h.01" /></>,
  leaf: <><path d="M5 19c0-8 5-13 14-14 0 9-5 14-13 14z" /><path d="M5 19l7-7" /></>,
  snow: <path d="M12 3v18M4.2 7.5l15.6 9M4.2 16.5l15.6-9M9 4l3 2 3-2M9 20l3-2 3 2" />,
  fish: <><path d="M3 12c3-4 8-5 12-3l4-3v12l-4-3c-4 2-9 1-12-3z" /><path d="M8 11h.01" /></>,
  clock: <><circle cx="12" cy="12" r="8" /><path d="M12 7v5l3 2" /></>,
  moon: <path d="M19 14.5A7.5 7.5 0 0 1 9.5 5 7.5 7.5 0 1 0 19 14.5z" />,
  bolt: <path d="M13 3L5 13h6l-1 8 8-10h-6z" />,
  school: <><path d="M3 9l9-4 9 4-9 4z" /><path d="M7 11v5c3 2 7 2 10 0v-5" /></>,
  chip: <><rect x="7" y="7" width="10" height="10" rx="2" /><path d="M10 3v4M14 3v4M10 17v4M14 17v4M3 10h4M3 14h4M17 10h4M17 14h4" /></>,
  phone: <><rect x="7" y="3" width="10" height="18" rx="2" /><path d="M11 18h2" /></>,
  users: <><circle cx="9" cy="8" r="3" /><path d="M3 20c0-3.5 2.7-6 6-6s6 2.5 6 6" /><path d="M16 5a3 3 0 0 1 0 6M21 20c0-2.8-1.6-5-4-5.7" /></>,
};

export default function Icon({ name, size = 26 }: { name: Name; size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      {paths[name]}
    </svg>
  );
}
