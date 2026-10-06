// The package ships types its export map does not point at; this is the one function the tests use.
declare module 'dom-accessibility-api' {
  export function computeAccessibleName(root: Element): string;
}
