"use client";

/** Opens WhatsApp with a prefilled message and the page link (no auto-send). */
export function ShareButton({ text, path }: { text: string; path: string }) {
  return (
    <button
      type="button"
      className="btn small ghost"
      onClick={() => {
        const url = `${window.location.origin}${path}`;
        window.open(`https://wa.me/?text=${encodeURIComponent(`${text}\n${url}`)}`, "_blank", "noopener");
      }}
    >
      Share on WhatsApp
    </button>
  );
}
