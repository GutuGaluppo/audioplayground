import { useEffect, useRef, useState } from 'react';

import { useBridge } from '../bridge/BridgeContext';
import type { ProjectAsset } from '../bridge/generated';
import { useLatest } from '../state/latestEvent';
import { useStores } from '../state/StoresContext';

const NO_ASSETS: readonly ProjectAsset[] = [];

/** What uses a file, in words: "2 clips · sampler · 1 pad", or "Unused". */
export function describeUse(asset: ProjectAsset): string {
  const parts: string[] = [];
  if (asset.clips > 0) parts.push(`${String(asset.clips)} clip${asset.clips === 1 ? '' : 's'}`);
  if (asset.sampler) parts.push('sampler');
  if (asset.pads > 0) parts.push(`${String(asset.pads)} pad${asset.pads === 1 ? '' : 's'}`);
  return parts.length > 0 ? parts.join(' · ') : 'Unused';
}

/** The project's audio files: what uses each, locate missing ones, take unused ones out. */
export function AudioFilesMenu() {
  const bridge = useBridge();
  const assets = useLatest(useStores().projectAssets)?.assets ?? NO_ASSETS;
  const [open, setOpen] = useState(false);
  const panel = useRef<HTMLDivElement>(null);
  const unused = assets.filter((a) => describeUse(a) === 'Unused');
  const missing = assets.filter((a) => a.missing).length;

  useEffect(() => {
    if (!open) return;
    const close = (event: PointerEvent) => {
      if (panel.current && !panel.current.contains(event.target as Node)) setOpen(false);
    };
    window.addEventListener('pointerdown', close);
    return () => {
      window.removeEventListener('pointerdown', close);
    };
  }, [open]);

  return (
    <div className="menu" ref={panel}>
      <button
        type="button"
        className="button button--quiet"
        aria-expanded={open}
        title="The audio files in this project"
        data-attention={missing > 0}
        onClick={() => {
          setOpen(!open);
        }}
      >
        Audio ({String(assets.length)})
      </button>
      {open ? (
        <div className="menu__list audio-files" role="dialog" aria-label="Project audio files">
          {assets.length === 0 ? (
            <p className="audio-files__empty">No audio files yet. Import or record some.</p>
          ) : (
            <ul>
              {assets.map((asset) => (
                <li key={asset.id} data-missing={asset.missing}>
                  <span className="audio-files__name">{asset.name}</span>
                  <span className="audio-files__use">
                    {asset.missing ? 'Missing · ' : ''}
                    {describeUse(asset)}
                  </span>
                  {asset.missing ? (
                    <button
                      type="button"
                      className="button button--quiet"
                      aria-label={`Locate ${asset.name}`}
                      onClick={() => {
                        bridge.send({ type: 'asset.locate', payload: { asset: asset.id } });
                      }}
                    >
                      Locate…
                    </button>
                  ) : null}
                  {describeUse(asset) === 'Unused' ? (
                    <button
                      type="button"
                      className="button button--quiet"
                      aria-label={`Remove ${asset.name}`}
                      onClick={() => {
                        bridge.send({ type: 'asset.remove', payload: { asset: asset.id } });
                      }}
                    >
                      Remove
                    </button>
                  ) : null}
                </li>
              ))}
            </ul>
          )}
          <button
            type="button"
            className="button"
            disabled={unused.length === 0}
            onClick={() => {
              bridge.send({ type: 'asset.removeUnused', payload: {} });
            }}
          >
            Remove unused ({String(unused.length)})
          </button>
          <p className="audio-files__note">
            Removing takes a file out of the project (undo brings it back). The file stays in the
            project's audio folder.
          </p>
        </div>
      ) : null}
    </div>
  );
}
