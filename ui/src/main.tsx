import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';

import { App } from './App';
import { createBridge } from './bridge/bridge';
import { BridgeContext } from './bridge/BridgeContext';
import { createStores } from './state/stores';
import { StoresContext } from './state/StoresContext';
import './styles/tokens.css';
import './styles/base.css';
import './styles/skin.css';

const rootElement = document.getElementById('root');
if (!rootElement) {
  throw new Error('Root element #root not found');
}

const bridge = createBridge();
const stores = createStores(bridge);

createRoot(rootElement).render(
  <StrictMode>
    <BridgeContext.Provider value={bridge}>
      <StoresContext.Provider value={stores}>
        <App />
      </StoresContext.Provider>
    </BridgeContext.Provider>
  </StrictMode>,
);
