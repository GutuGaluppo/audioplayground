import { useState, useSyncExternalStore } from 'react';

import { useBridge } from '../bridge/BridgeContext';
import type { ProjectLibrary } from '../web/projectLibrary';
import type { ProjectInfo } from '../web/projectStore';
import { MAX_NAME_LENGTH } from '../web/projectLibrary';
import { Modal } from './Modal';

const noLibrary = () => () => undefined;

const when = (ms: number) =>
  new Date(ms).toLocaleString(undefined, { dateStyle: 'medium', timeStyle: 'short' });
const size = (bytes: number) =>
  bytes < 1024 * 100 ? `${(bytes / 1024).toFixed(1)} KB` : `${String(Math.round(bytes / 1024))} KB`;

function OpenDialog({
  library,
  projects,
}: {
  library: ProjectLibrary;
  projects: readonly ProjectInfo[];
}) {
  const [confirming, setConfirming] = useState<string | null>(null);
  return (
    <Modal
      title="Open a song"
      onCancel={() => {
        library.cancel();
      }}
    >
      {projects.length === 0 ? (
        <p className="dialog__text">
          No saved songs yet. Use Save to keep this one in your browser.
        </p>
      ) : (
        <ul className="dialog__list">
          {projects.map((project) => (
            <li key={project.id}>
              <button
                type="button"
                className="dialog__song"
                data-autofocus={project === projects[0] ? '' : undefined}
                onClick={() => {
                  void library.openProject(project.id);
                }}
              >
                <strong>{project.name}</strong>
                <span>
                  {when(project.updatedAt)} · {size(project.bytes)}
                </span>
              </button>
              {confirming === project.id ? (
                <span className="dialog__confirm">
                  <button
                    type="button"
                    className="button"
                    onClick={() => {
                      setConfirming(null);
                      void library.deleteProject(project.id);
                    }}
                  >
                    Delete “{project.name}”
                  </button>
                  <button
                    type="button"
                    className="button button--quiet"
                    onClick={() => {
                      setConfirming(null);
                    }}
                  >
                    Keep
                  </button>
                </span>
              ) : (
                <button
                  type="button"
                  className="button button--quiet"
                  aria-label={`Delete ${project.name}`}
                  onClick={() => {
                    setConfirming(project.id);
                  }}
                >
                  Delete
                </button>
              )}
            </li>
          ))}
        </ul>
      )}
      <p className="dialog__note">
        Songs are kept in this browser, on this device. Clearing the browser&apos;s site data
        removes them.
      </p>
      <div className="dialog__actions">
        <button
          type="button"
          className="button button--quiet"
          data-autofocus={projects.length === 0 ? '' : undefined}
          onClick={() => {
            library.cancel();
          }}
        >
          Cancel
        </button>
      </div>
    </Modal>
  );
}

function SaveAsDialog({
  library,
  initial,
  existing,
}: {
  library: ProjectLibrary;
  initial: string;
  existing: readonly ProjectInfo[];
}) {
  const [name, setName] = useState(initial);
  const clean = name.trim();
  const replaces = existing.some((p) => p.name.toLowerCase() === clean.toLowerCase());
  return (
    <Modal
      title="Save song"
      onCancel={() => {
        library.cancel();
      }}
    >
      <form
        className="dialog__form"
        onSubmit={(event) => {
          event.preventDefault();
          if (clean) void library.saveAs(clean);
        }}
      >
        <label>
          Name
          <input
            data-autofocus=""
            value={name}
            maxLength={MAX_NAME_LENGTH}
            onFocus={(event) => {
              event.currentTarget.select();
            }}
            onChange={(event) => {
              setName(event.currentTarget.value);
            }}
          />
        </label>
        {replaces ? (
          <p className="dialog__note" role="status">
            A song with this name is already saved. Saving replaces it.
          </p>
        ) : null}
        <div className="dialog__actions">
          <button
            type="button"
            className="button button--quiet"
            onClick={() => {
              library.cancel();
            }}
          >
            Cancel
          </button>
          <button type="submit" className="button" disabled={clean === ''}>
            {replaces ? 'Replace' : 'Save'}
          </button>
        </div>
      </form>
    </Modal>
  );
}

/** The questions the browser version asks about where songs are kept. Nothing on the desktop. */
export function ProjectDialogs() {
  const library = useBridge().projects;
  const dialog = useSyncExternalStore(
    (listener) => (library ? library.subscribe(listener) : noLibrary()),
    () => library?.dialog() ?? null,
  );
  if (!library || !dialog) return null;

  switch (dialog.kind) {
    case 'open':
      return <OpenDialog library={library} projects={dialog.projects} />;
    case 'saveAs':
      return <SaveAsDialog library={library} initial={dialog.name} existing={dialog.existing} />;
    case 'discard':
      return (
        <Modal
          title={`Save changes to “${dialog.name}”?`}
          onCancel={() => {
            void library.answerDiscard('cancel');
          }}
        >
          <p className="dialog__text">Your changes will be lost if you don&apos;t save them.</p>
          <div className="dialog__actions">
            <button
              type="button"
              className="button button--quiet"
              onClick={() => {
                void library.answerDiscard('cancel');
              }}
            >
              Cancel
            </button>
            <button
              type="button"
              className="button button--quiet"
              onClick={() => {
                void library.answerDiscard('discard');
              }}
            >
              Don&apos;t save
            </button>
            <button
              type="button"
              className="button"
              data-autofocus=""
              onClick={() => {
                void library.answerDiscard('save');
              }}
            >
              Save
            </button>
          </div>
        </Modal>
      );
    case 'recover':
      return (
        <Modal
          title="Restore your unsaved work?"
          onCancel={() => {
            void library.answerRecover(false);
          }}
        >
          <p className="dialog__text">
            Last time, “{dialog.name}” had changes that were not saved ({when(dialog.at)}).
          </p>
          <div className="dialog__actions">
            <button
              type="button"
              className="button button--quiet"
              onClick={() => {
                void library.answerRecover(false);
              }}
            >
              Discard
            </button>
            <button
              type="button"
              className="button"
              data-autofocus=""
              onClick={() => {
                void library.answerRecover(true);
              }}
            >
              Restore
            </button>
          </div>
        </Modal>
      );
  }
}
