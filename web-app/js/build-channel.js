// Which deployment is serving this page. The dev and release configurators are
// byte-identical trees copied to different paths by the docs workflow, so the
// URL is the only signal there is: an `app-dev` path segment means `main`.

export function buildChannel(pathname = '') {
  return String(pathname).split('/').includes('app-dev') ? 'dev' : 'release';
}
