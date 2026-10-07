import { useSyncExternalStore } from 'react';

import { useBridge } from '../bridge/BridgeContext';
import { describeExport } from '../web/exporter';
import { Modal } from './Modal';

const noExport = () => () => undefined;

/**
 * A finished export in the browser version. A browser only starts a download from a click, so the
 * song waits here until it is clicked, and says what it holds (length, loudness, peak).
 */
export function ExportReady() {
  const exports = useBridge().exportFile;
  const result = useSyncExternalStore(
    (listener) => (exports ? exports.subscribe(listener) : noExport()),
    () => exports?.result() ?? null,
  );
  if (!exports || !result) return null;

  return (
    <Modal
      title="Your song is ready"
      onCancel={() => {
        exports.dismiss();
      }}
    >
      <p className="dialog__text">
        <strong>{result.fileName}</strong>
        <br />
        {describeExport(result)}
      </p>
      {result.missingAudio > 0 ? (
        <p className="dialog__note" role="status">
          {result.missingAudio === 1
            ? 'One audio file could not be found, so it is silent in this file.'
            : `${String(result.missingAudio)} audio files could not be found, so they are silent in this file.`}
        </p>
      ) : null}
      <div className="dialog__actions">
        <button
          type="button"
          className="button button--quiet"
          onClick={() => {
            exports.dismiss();
          }}
        >
          Close
        </button>
        <button
          type="button"
          className="button"
          data-autofocus=""
          onClick={() => {
            exports.download();
          }}
        >
          Download
        </button>
      </div>
    </Modal>
  );
}
