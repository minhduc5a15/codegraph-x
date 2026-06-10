import {
  NODE_RECORD_SIZE,
  NODE_OFFSET_ID,
  NODE_OFFSET_NAME_POOL,
  NODE_OFFSET_PATH_POOL,
  NODE_OFFSET_START_LINE,
  NODE_OFFSET_END_LINE,
  NODE_OFFSET_TYPE,
} from './constants.js';

export enum NodeType {
  FILE = 0,
  CLASS = 1,
  FUNCTION = 2,
  METHOD = 3,
  EXTERNAL = 4,
}

let _addon: any = null;
export function setNativeAddon(addon: any) {
  _addon = addon;
}

export enum EdgeType {
  CALLS = 0,
  INHERITS = 1,
  IMPORTS = 2,
  AMBIGUOUS_CALL = 3,
}

export interface GraphData {
  nodes: ArrayBuffer;
  offsets: ArrayBuffer;
  edges: ArrayBuffer;
  stringPool: ArrayBuffer;
  nameIndex: ArrayBuffer;
  shortNameIndex: ArrayBuffer;
  pathIndex: ArrayBuffer;
  incomingOffsets: ArrayBuffer;
  incomingEdges: ArrayBuffer;
}

export class EdgeCursor {
  private currentByteOffset: number = 0;

  constructor(private edgesView: DataView) {}

  public moveTo(index: number): void {
    this.currentByteOffset = index * 8;
  }

  public get targetId(): number {
    return this.edgesView.getUint32(this.currentByteOffset, true);
  }

  public get type(): EdgeType {
    return this.edgesView.getUint8(this.currentByteOffset + 4) as EdgeType;
  }
}

export class Codegraph {
  private nodesView: DataView;
  private offsets: Uint32Array;
  private edgesView: DataView;
  private stringPool: Uint8Array;
  private textDecoder: TextDecoder;
  private sharedEdgeCursor: EdgeCursor;
  private nameIndexView: Uint32Array;
  private shortNameIndexView: Uint32Array;
  private pathIndexView: Uint32Array;
  private incomingOffsets: Uint32Array;
  private incomingEdges: Uint32Array;

  constructor(data: GraphData) {
    this.nodesView = new DataView(data.nodes);
    this.offsets = new Uint32Array(data.offsets);
    this.edgesView = new DataView(data.edges);
    this.stringPool = new Uint8Array(data.stringPool);
    this.nameIndexView = new Uint32Array(data.nameIndex);
    this.shortNameIndexView = new Uint32Array(data.shortNameIndex);
    this.pathIndexView = new Uint32Array(data.pathIndex);
    this.incomingOffsets = new Uint32Array(data.incomingOffsets);
    this.incomingEdges = new Uint32Array(data.incomingEdges);
    this.textDecoder = new TextDecoder('utf-8');
    this.sharedEdgeCursor = new EdgeCursor(this.edgesView);
  }

  private resolveNameForNode(nodeId: number): string {
    const byteOffset = nodeId * NODE_RECORD_SIZE;
    const name_pool_offset = this.nodesView.getUint32(byteOffset + NODE_OFFSET_NAME_POOL, true);
    return this.resolveString(name_pool_offset);
  }

  private resolvePathForNode(nodeId: number): string {
    const byteOffset = nodeId * NODE_RECORD_SIZE;
    const path_pool_offset = this.nodesView.getUint32(byteOffset + NODE_OFFSET_PATH_POOL, true);
    return this.resolveString(path_pool_offset);
  }

  public resolveString(offset: number): string {
    if (offset >= this.stringPool.length) return '';
    let end = offset;
    while (end < this.stringPool.length && this.stringPool[end] !== 0) {
      end++;
    }
    return this.textDecoder.decode(this.stringPool.subarray(offset, end));
  }

  public getNode(nodeId: number) {
    const byteOffset = nodeId * NODE_RECORD_SIZE;
    if (byteOffset + NODE_RECORD_SIZE > this.nodesView.byteLength) {
      throw new Error('Node ID out of bounds');
    }

    const name_pool_offset = this.nodesView.getUint32(byteOffset + NODE_OFFSET_NAME_POOL, true);
    const path_pool_offset = this.nodesView.getUint32(byteOffset + NODE_OFFSET_PATH_POOL, true);

    return {
      id: this.nodesView.getUint32(byteOffset + NODE_OFFSET_ID, true),
      name: this.resolveString(name_pool_offset),
      path: this.resolveString(path_pool_offset),
      startLine: this.nodesView.getUint32(byteOffset + NODE_OFFSET_START_LINE, true),
      endLine: this.nodesView.getUint32(byteOffset + NODE_OFFSET_END_LINE, true),
      type: this.nodesView.getUint8(byteOffset + NODE_OFFSET_TYPE) as NodeType,
    };
  }

  public getEdgeCursor(nodeId: number) {
    const nodeCount = this.offsets.length - 1;
    if (nodeId >= nodeCount) {
      return { cursor: this.sharedEdgeCursor, startIdx: 0, endIdx: 0 };
    }

    return {
      cursor: this.sharedEdgeCursor,
      startIdx: this.offsets[nodeId],
      endIdx: this.offsets[nodeId + 1],
    };
  }

  public get nodeCount(): number {
    return this.offsets.length - 1;
  }

  private extractShortName(fqn: string): string {
    const pos = fqn.lastIndexOf('::');
    return pos === -1 ? fqn : fqn.substring(pos + 2);
  }

  public searchNodesByName(nameMatch: string): any[] {
    const results: any[] = [];
    if (!nameMatch) return results;

    const useShortName = !nameMatch.includes('::');
    const targetIndexView = useShortName ? this.shortNameIndexView : this.nameIndexView;

    let low = 0;
    let high = targetIndexView.length - 1;
    let firstMatchIdx = -1;

    while (low <= high) {
      const mid = (low + high) >>> 1;
      const midId = targetIndexView[mid];
      let midName = this.resolveNameForNode(midId);
      if (useShortName) midName = this.extractShortName(midName);

      if (midName.startsWith(nameMatch)) {
        firstMatchIdx = mid;
        high = mid - 1; // Look left for the first occurrence
      } else if (midName < nameMatch) {
        low = mid + 1;
      } else {
        high = mid - 1;
      }
    }

    if (firstMatchIdx !== -1) {
      for (let i = firstMatchIdx; i < targetIndexView.length && results.length < 100; i++) {
        const id = targetIndexView[i];
        let name = this.resolveNameForNode(id);
        if (useShortName) name = this.extractShortName(name);
        if (name.startsWith(nameMatch)) {
          results.push(this.getNode(id));
        } else {
          break;
        }
      }
    }

    return results;
  }

  public getNodesByFile(pathMatch: string): any[] {
    const results: any[] = [];
    if (!pathMatch) return results;

    if (_addon && _addon.SearchPathSubstring) {
      const ids = _addon.SearchPathSubstring(pathMatch);
      for (const id of ids) {
        results.push(this.getNode(id));
      }
    } else {
      for (let i = 0; i < this.pathIndexView.length; i++) {
        const id = this.pathIndexView[i];
        const path = this.resolvePathForNode(id);
        if (path.includes(pathMatch)) {
          results.push(this.getNode(id));
          if (results.length >= 100) break;
        }
      }
    }
    return results;
  }

  private processNeighbors(id: number, depth: number, maxDepth: number, visited: Set<number>, queue: { id: number; depth: number }[]) {
    const { cursor, startIdx, endIdx } = this.getEdgeCursor(id);
    const neighbors = [];

    for (let i = startIdx; i < endIdx; i++) {
      cursor.moveTo(i);
      const targetId = cursor.targetId;
      let targetName = 'Unknown';

      const byteOffset = targetId * NODE_RECORD_SIZE;
      if (byteOffset + NODE_RECORD_SIZE <= this.nodesView.byteLength) {
        targetName = this.resolveNameForNode(targetId);
      }

      neighbors.push({
        targetId: targetId,
        type: cursor.type,
        name: targetName,
      });

      if (depth < maxDepth && !visited.has(targetId)) {
        visited.add(targetId);
        queue.push({ id: targetId, depth: depth + 1 });
      }
    }
    return neighbors;
  }

  private processCallers(id: number, depth: number, maxDepth: number, visited: Set<number>, queue: { id: number; depth: number }[]) {
    const callers = [];
    const callerStartIdx = id < this.incomingOffsets.length - 1 ? this.incomingOffsets[id] : 0;
    const callerEndIdx = id < this.incomingOffsets.length - 1 ? this.incomingOffsets[id + 1] : 0;

    for (let i = callerStartIdx; i < callerEndIdx; i++) {
      const callerId = this.incomingEdges[i];
      let callerName = 'Unknown';

      const byteOffset = callerId * NODE_RECORD_SIZE;
      if (byteOffset + NODE_RECORD_SIZE <= this.nodesView.byteLength) {
        callerName = this.resolveNameForNode(callerId);
      }

      callers.push({
        sourceId: callerId,
        name: callerName,
      });

      if (depth < maxDepth && !visited.has(callerId)) {
        visited.add(callerId);
        queue.push({ id: callerId, depth: depth + 1 });
      }
    }
    return callers;
  }

  public exploreFlow(symbols: string[], maxDepth: number = 3): any[] {
    const visited = new Set<number>();
    const flowGraph: any[] = [];
    const queue: { id: number; depth: number }[] = [];

    // Find starting nodes
    for (const sym of symbols) {
      const nodes = this.searchNodesByName(sym);
      let addedCount = 0;
      for (const node of nodes) {
        if (!visited.has(node.id)) {
          visited.add(node.id);
          queue.push({ id: node.id, depth: 0 });
          addedCount++;
          if (addedCount >= 50) break;
        }
      }
    }

    // Safety limit on starting nodes
    if (queue.length > 50) {
      queue.length = 50;
    }

    let head = 0;
    while (head < queue.length) {
      const { id, depth } = queue[head++];

      const byteOffset = id * NODE_RECORD_SIZE;
      if (byteOffset + NODE_RECORD_SIZE > this.nodesView.byteLength) {
        continue; // Prevent Out of bounds
      }

      const nodeInfo = this.getNode(id);
      const neighbors = this.processNeighbors(id, depth, maxDepth, visited, queue);
      const callers = this.processCallers(id, depth, maxDepth, visited, queue);

      flowGraph.push({
        ...nodeInfo,
        depth,
        neighbors,
        callers,
      });

      // Hard limit for context window safety and UI performance
      if (flowGraph.length >= 100) break;
    }

    return flowGraph;
  }
}
