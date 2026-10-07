/** Hands a file to the browser's download, under the name given. Call from a click. */
export function downloadFile(fileName: string, bytes: Uint8Array, type = 'audio/wav'): void {
  const url = URL.createObjectURL(new Blob([bytes as BlobPart], { type }));
  const link = document.createElement('a');
  link.href = url;
  link.download = fileName;
  link.style.display = 'none';
  document.body.append(link);
  link.click();
  link.remove();
  // The download has started by the next task; let the browser have the URL a little longer to be safe.
  setTimeout(() => {
    URL.revokeObjectURL(url);
  }, 60_000);
}
