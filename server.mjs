import { createReadStream, existsSync, statSync } from 'node:fs';
import { extname, join, normalize, resolve } from 'node:path';
import { createServer } from 'node:http';
import { WebSocketServer } from 'ws';

const host = process.env.HOST ?? '127.0.0.1';
const port = Number(process.env.PORT ?? 4173);
const dist = resolve('dist');

const mimeTypes = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.wasm': 'application/wasm',
  '.css': 'text/css; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.webp': 'image/webp',
  '.ico': 'image/x-icon',
  '.json': 'application/json; charset=utf-8',
};

function sendFile(response, path) {
  const ext = extname(path);
  response.writeHead(200, {
    'content-type': mimeTypes[ext] ?? 'application/octet-stream',
    'cache-control': ext === '.html' ? 'no-cache' : 'public, max-age=31536000, immutable',
  });
  createReadStream(path).pipe(response);
}

const server = createServer((request, response) => {
  const url = new URL(request.url ?? '/', `http://${request.headers.host ?? 'localhost'}`);
  if (url.pathname === '/healthz') {
    response.writeHead(200, { 'content-type': 'application/json' });
    response.end(JSON.stringify({ ok: true, app: 'rocket-sim' }));
    return;
  }

  const requested = normalize(decodeURIComponent(url.pathname)).replace(/^(\.\.[/\\])+/, '');
  const candidate = join(dist, requested === '/' ? 'index.html' : requested);
  const file = existsSync(candidate) && statSync(candidate).isFile() ? candidate : join(dist, 'index.html');
  sendFile(response, file);
});

const rooms = new Map();
const sockets = new Map();
const wss = new WebSocketServer({ server, path: '/ws' });

wss.on('connection', (socket, request) => {
  const url = new URL(request.url ?? '/', `http://${request.headers.host ?? 'localhost'}`);
  const room = url.searchParams.get('room') || 'mission-control';
  const peer = url.searchParams.get('peer') || `peer-${Math.random().toString(36).slice(2, 8)}`;
  sockets.set(socket, { room, peer });
  if (!rooms.has(room)) rooms.set(room, new Set());
  rooms.get(room).add(socket);

  socket.on('message', (buffer) => {
    const meta = sockets.get(socket);
    if (!meta) return;
    const peers = rooms.get(meta.room);
    if (!peers) return;
    for (const other of peers) {
      if (other !== socket && other.readyState === other.OPEN) {
        other.send(buffer.toString());
      }
    }
  });

  socket.on('close', () => {
    const meta = sockets.get(socket);
    sockets.delete(socket);
    if (!meta) return;
    const peers = rooms.get(meta.room);
    peers?.delete(socket);
    if (peers?.size === 0) rooms.delete(meta.room);
  });
});

server.listen(port, host, () => {
  console.log(`Rocket Sim listening on http://${host}:${port}`);
});
