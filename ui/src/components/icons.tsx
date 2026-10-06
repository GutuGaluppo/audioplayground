/** Category glyphs (outline, currentColor). Colour comes from the surrounding category. */
const common = {
  viewBox: '0 0 24 24',
  fill: 'none',
  stroke: 'currentColor',
  strokeWidth: 1.6,
  strokeLinecap: 'round',
  strokeLinejoin: 'round',
  'aria-hidden': true,
  focusable: false,
} as const;

export function DrumIcon({ className }: { className?: string }) {
  return (
    <svg {...common} className={className}>
      <ellipse cx="12" cy="13" rx="7" ry="3" />
      <path d="M5 13v4c0 1.7 3.1 3 7 3s7-1.3 7-3v-4" />
      <path d="M4 4l6 6M20 4l-6 6" />
    </svg>
  );
}

export function SynthIcon({ className }: { className?: string }) {
  return (
    <svg {...common} className={className}>
      <rect x="3" y="6" width="18" height="12" rx="1.5" />
      <path d="M8 18v-5M12 18v-5M16 18v-5M6.5 6v7h3V6M14.5 6v7h3V6" />
    </svg>
  );
}

export function SamplerIcon({ className }: { className?: string }) {
  return (
    <svg {...common} className={className}>
      <path d="M4 12v0M7 9v6M10 5v14M13 8v8M16 10v4M19 11.5v1" />
    </svg>
  );
}

export function MicIcon({ className }: { className?: string }) {
  return (
    <svg {...common} className={className}>
      <rect x="9" y="3" width="6" height="11" rx="3" />
      <path d="M5.5 11a6.5 6.5 0 0 0 13 0M12 17.5V21M8.5 21h7" />
    </svg>
  );
}
