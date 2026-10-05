// Minimal typing of the public JUCE WebView integration object injected by WebBrowserComponent
// when native integration is enabled.
interface JuceBackend {
  addEventListener(eventId: string, listener: (payload: unknown) => void): [string, number];
  removeEventListener(token: [string, number]): void;
  emitEvent(eventId: string, payload: unknown): void;
}

interface JuceGlobal {
  readonly backend: JuceBackend;
  readonly initialisationData: Readonly<Record<string, unknown>>;
}

interface Window {
  readonly __JUCE__?: JuceGlobal;
}
