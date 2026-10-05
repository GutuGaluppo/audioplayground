import { cleanup } from '@testing-library/react';
import { afterEach } from 'vitest';

afterEach(() => {
  cleanup();
});

// jsdom does not implement pointer capture; the UI releases it so drags can glide across keys.
if (!('releasePointerCapture' in Element.prototype)) {
  Object.defineProperty(Element.prototype, 'releasePointerCapture', { value: () => undefined });
}
