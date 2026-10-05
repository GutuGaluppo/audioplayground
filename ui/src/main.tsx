import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';

import { App } from './App';
import { createBridge } from './bridge/bridge';
import { BridgeContext } from './bridge/BridgeContext';
import { createEngineStatusStore } from './state/engineStatus';
import './styles/tokens.css';
import './styles/base.css';

const rootElement = document.getElementById('root');
if (!rootElement) {
  throw new Error('Root element #root not found');
}

const bridge = createBridge();
const statusStore = createEngineStatusStore(bridge);

createRoot(rootElement).render(
  <StrictMode>
    <BridgeContext.Provider value={bridge}>
      <App statusStore={statusStore} />
    </BridgeContext.Provider>
  </StrictMode>,
);
