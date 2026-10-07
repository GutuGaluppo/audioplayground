import { useEffect, useState } from 'react';

import { useBridge } from '../bridge/BridgeContext';
import { useLatest } from '../state/latestEvent';
import { useStores } from '../state/StoresContext';
import { AudioFilesMenu } from './AudioFilesMenu';
import { ExportButton } from './ExportButton';

const isMac = typeof navigator !== 'undefined' && /Mac|iPhone|iPad/.test(navigator.userAgent);
const mod = isMac ? '⌘' : 'Ctrl+';

/** Project name (click to rename), unsaved indicator, New / Open / Save and their shortcuts. */
export function ProjectHeader() {
  const bridge = useBridge();
  const project = useLatest(useStores().project);
  const [draft, setDraft] = useState<string | null>(null);

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      if (!(isMac ? event.metaKey : event.ctrlKey) || event.altKey) return;
      const key = event.key.toLowerCase();
      const intent =
        key === 's'
          ? event.shiftKey
            ? ('project.saveAs' as const)
            : ('project.save' as const)
          : key === 'o' && !event.shiftKey
            ? ('project.open' as const)
            : key === 'n' && !event.shiftKey
              ? ('project.new' as const)
              : null;
      if (!intent) return;
      event.preventDefault();
      bridge.send({ type: intent, payload: {} });
    };
    window.addEventListener('keydown', onKeyDown);
    return () => {
      window.removeEventListener('keydown', onKeyDown);
    };
  }, [bridge]);

  const commitName = () => {
    if (draft !== null && project && draft.trim() !== '' && draft !== project.name) {
      bridge.send({ type: 'project.rename', payload: { name: draft.slice(0, 128) } });
    }
    setDraft(null);
  };

  return (
    <div className="project">
      {draft === null ? (
        <button
          type="button"
          className="project__name"
          title="Rename project"
          disabled={!project}
          onClick={() => {
            setDraft(project?.name ?? '');
          }}
        >
          {project?.name ?? ''}
        </button>
      ) : (
        <input
          className="project__name-input"
          aria-label="Project name"
          maxLength={128}
          autoFocus
          value={draft}
          onChange={(event) => {
            setDraft(event.currentTarget.value);
          }}
          onBlur={commitName}
          onKeyDown={(event) => {
            if (event.key === 'Enter') event.currentTarget.blur();
            if (event.key === 'Escape') setDraft(null);
          }}
        />
      )}
      {project?.dirty ? (
        <span
          className="project__dirty"
          role="status"
          aria-label="Unsaved changes"
          title="Unsaved changes"
        />
      ) : null}

      <div className="project__actions">
        <button
          type="button"
          className="button button--quiet"
          title={`New project (${mod}N)`}
          onClick={() => {
            bridge.send({ type: 'project.new', payload: {} });
          }}
        >
          New
        </button>
        <button
          type="button"
          className="button button--quiet"
          title={`Open project (${mod}O)`}
          onClick={() => {
            bridge.send({ type: 'project.open', payload: {} });
          }}
        >
          Open
        </button>
        <button
          type="button"
          className="button button--quiet"
          title={`Save (${mod}S)`}
          onClick={() => {
            bridge.send({ type: 'project.save', payload: {} });
          }}
        >
          Save
        </button>
        <AudioFilesMenu />
        <ExportButton />
      </div>
    </div>
  );
}
