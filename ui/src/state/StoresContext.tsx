import { createContext, useContext } from 'react';

import type { Stores } from './stores';

export const StoresContext = createContext<Stores | null>(null);

export function useStores(): Stores {
  const stores = useContext(StoresContext);
  if (!stores) throw new Error('useStores must be used inside <StoresContext.Provider>');
  return stores;
}
