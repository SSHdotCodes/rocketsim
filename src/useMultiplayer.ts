import { useEffect, useMemo, useRef, useState } from 'react';
import type { FlightState, PeerState, RocketDesign, View } from './types';

type MultiplayerMessage =
  | { type: 'hello'; peer: PeerState }
  | { type: 'state'; peer: PeerState }
  | { type: 'design'; design: RocketDesign; peer: PeerState };

interface MultiplayerArgs {
  room: string;
  enabled: boolean;
  view: View;
  design: RocketDesign;
  flight: FlightState;
  onDesign: (design: RocketDesign) => void;
}

const clientId = `pilot-${Math.random().toString(36).slice(2, 8)}`;

export function useMultiplayer({
  room,
  enabled,
  view,
  design,
  flight,
  onDesign,
}: MultiplayerArgs) {
  const [status, setStatus] = useState<'offline' | 'connecting' | 'connected' | 'fallback'>('offline');
  const [peers, setPeers] = useState<Record<string, PeerState>>({});
  const socketRef = useRef<WebSocket | null>(null);
  const channelRef = useRef<BroadcastChannel | null>(null);
  const argsRef = useRef({ view, design, flight, onDesign });
  const selfRef = useRef<PeerState | null>(null);

  argsRef.current = { view, design, flight, onDesign };

  const self = useMemo<PeerState>(
    () => ({
      id: clientId,
      name: `Pilot ${clientId.slice(-3).toUpperCase()}`,
      view,
      designName: design.name,
      altitude: Math.round(flight.altitude),
      speed: Math.round(flight.speed),
      updatedAt: Date.now(),
    }),
    [design.name, flight.altitude, flight.speed, view],
  );

  useEffect(() => {
    selfRef.current = self;
  }, [self]);

  useEffect(() => {
    if (!enabled || !room.trim()) {
      socketRef.current?.close();
      channelRef.current?.close();
      setStatus('offline');
      return;
    }

    let closed = false;
    const currentPeer = () =>
      selfRef.current ?? {
        id: clientId,
        name: `Pilot ${clientId.slice(-3).toUpperCase()}`,
        view: argsRef.current.view,
        designName: argsRef.current.design.name,
        altitude: Math.round(argsRef.current.flight.altitude),
        speed: Math.round(argsRef.current.flight.speed),
        updatedAt: Date.now(),
      };
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/ws?room=${encodeURIComponent(room.trim())}&peer=${clientId}`;

    const handleMessage = (message: MultiplayerMessage) => {
      if ('peer' in message && message.peer.id !== clientId) {
        setPeers((current) => ({
          ...current,
          [message.peer.id]: message.peer,
        }));
      }
      if (message.type === 'design' && message.peer.id !== clientId) {
        argsRef.current.onDesign({
          ...message.design,
          name: `${message.design.name} (co-op)`,
          updatedAt: Date.now(),
        });
      }
    };

    const fallback = () => {
      if (closed) return;
      setStatus('fallback');
      const channel = new BroadcastChannel(`rocket-sim-${room.trim()}`);
      channel.onmessage = (event) => handleMessage(event.data as MultiplayerMessage);
      channelRef.current = channel;
      channel.postMessage({ type: 'hello', peer: currentPeer() } satisfies MultiplayerMessage);
    };

    try {
      setStatus('connecting');
      const socket = new WebSocket(wsUrl);
      socketRef.current = socket;
      socket.onopen = () => {
        setStatus('connected');
        socket.send(JSON.stringify({ type: 'hello', peer: currentPeer() } satisfies MultiplayerMessage));
      };
      socket.onmessage = (event) => handleMessage(JSON.parse(event.data) as MultiplayerMessage);
      socket.onerror = fallback;
      socket.onclose = () => {
        if (!closed && !channelRef.current) fallback();
      };
    } catch {
      fallback();
    }

    return () => {
      closed = true;
      socketRef.current?.close();
      channelRef.current?.close();
    };
  }, [enabled, room]);

  useEffect(() => {
    if (!enabled || status === 'offline') return;
    const message = { type: 'state', peer: self } satisfies MultiplayerMessage;
    const timer = window.setInterval(() => {
      const payload = JSON.stringify({ type: 'state', peer: { ...self, updatedAt: Date.now() } });
      if (socketRef.current?.readyState === WebSocket.OPEN) {
        socketRef.current.send(payload);
      }
      channelRef.current?.postMessage(message);
    }, 850);
    return () => window.clearInterval(timer);
  }, [enabled, self, status]);

  const broadcastDesign = (nextDesign: RocketDesign) => {
    if (!enabled || status === 'offline') return;
    const message = {
      type: 'design',
      design: nextDesign,
      peer: { ...self, updatedAt: Date.now(), designName: nextDesign.name },
    } satisfies MultiplayerMessage;
    const payload = JSON.stringify(message);
    if (socketRef.current?.readyState === WebSocket.OPEN) {
      socketRef.current.send(payload);
    }
    channelRef.current?.postMessage(message);
  };

  return {
    clientId,
    status,
    peers: Object.values(peers).filter((peer) => Date.now() - peer.updatedAt < 9000),
    broadcastDesign,
  };
}
