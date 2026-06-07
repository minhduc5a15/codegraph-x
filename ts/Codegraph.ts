export enum NodeType {
  FILE = 0,
  CLASS = 1,
  FUNCTION = 2,
  METHOD = 3,
}

export enum EdgeType {
  CALLS = 0,
  INHERITS = 1,
  IMPORTS = 2,
}

export interface GraphData {
  nodes: ArrayBuffer;
  offsets: ArrayBuffer;
  edges: ArrayBuffer;
  stringPool: ArrayBuffer;
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
  private nameIndex: Map<string, number[]> = new Map();
  private pathIndex: Map<string, number[]> = new Map();

  constructor(data: GraphData) {
    this.nodesView = new DataView(data.nodes);
    this.offsets = new Uint32Array(data.offsets);
    this.edgesView = new DataView(data.edges);
    this.stringPool = new Uint8Array(data.stringPool);
    this.textDecoder = new TextDecoder("utf-8");
    this.sharedEdgeCursor = new EdgeCursor(this.edgesView);
    this.buildIndex();
  }

  private buildIndex(): void {
    const count = this.nodeCount;
    for (let i = 0; i < count; i++) {
      const byteOffset = i * 24;
      const name_pool_offset = this.nodesView.getUint32(byteOffset + 4, true);
      const path_pool_offset = this.nodesView.getUint32(byteOffset + 8, true);
      
      const name = this.resolveString(name_pool_offset);
      const path = this.resolveString(path_pool_offset);

      if (name) {
        let arr = this.nameIndex.get(name);
        if (!arr) {
          arr = [];
          this.nameIndex.set(name, arr);
        }
        arr.push(i);
      }

      if (path) {
        let arr = this.pathIndex.get(path);
        if (!arr) {
          arr = [];
          this.pathIndex.set(path, arr);
        }
        arr.push(i);
      }
    }
  }

  public resolveString(offset: number): string {
    if (offset >= this.stringPool.length) return "";
    let end = offset;
    while (end < this.stringPool.length && this.stringPool[end] !== 0) {
      end++;
    }
    return this.textDecoder.decode(this.stringPool.subarray(offset, end));
  }

  public getNode(nodeId: number) {
    const byteOffset = nodeId * 24;
    if (byteOffset + 24 > this.nodesView.byteLength) {
      throw new Error("Node ID out of bounds");
    }

    const name_pool_offset = this.nodesView.getUint32(byteOffset + 4, true);
    const path_pool_offset = this.nodesView.getUint32(byteOffset + 8, true);

    return {
      id: this.nodesView.getUint32(byteOffset, true),
      name: this.resolveString(name_pool_offset),
      path: this.resolveString(path_pool_offset),
      startLine: this.nodesView.getUint32(byteOffset + 12, true),
      endLine: this.nodesView.getUint32(byteOffset + 16, true),
      type: this.nodesView.getUint8(byteOffset + 20) as NodeType,
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

  public searchNodesByName(nameMatch: string): any[] {
    const results = [];
    const exactMatches = this.nameIndex.get(nameMatch);
    if (exactMatches) {
      for (const id of exactMatches) {
        results.push(this.getNode(id));
      }
      return results;
    }
    
    // Fallback to substring matching if exact match fails
    for (const [name, ids] of this.nameIndex.entries()) {
      if (name.includes(nameMatch)) {
        for (const id of ids) {
          results.push(this.getNode(id));
        }
      }
      // Limit to 100 results to avoid massive payloads
      if (results.length >= 100) break;
    }
    return results;
  }

  public getNodesByFile(pathMatch: string): any[] {
    const results = [];
    for (const [path, ids] of this.pathIndex.entries()) {
      if (path.includes(pathMatch)) {
        for (const id of ids) {
          results.push(this.getNode(id));
        }
      }
    }
    return results;
  }

  public exploreFlow(symbols: string[], maxDepth: number = 3): any[] {
    const visited = new Set<number>();
    const flowGraph: any[] = [];
    const queue: { id: number; depth: number }[] = [];

    // Find starting nodes
    for (const sym of symbols) {
      const ids = this.nameIndex.get(sym);
      if (ids) {
        for (const id of ids) {
          if (!visited.has(id)) {
            visited.add(id);
            queue.push({ id, depth: 0 });
          }
        }
      } else {
        // substring match fallback
        for (const [name, nameIds] of this.nameIndex.entries()) {
          if (name.includes(sym)) {
            for (const id of nameIds) {
              if (!visited.has(id)) {
                visited.add(id);
                queue.push({ id, depth: 0 });
              }
            }
          }
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
      const nodeInfo = this.getNode(id);
      
      const { cursor, startIdx, endIdx } = this.getEdgeCursor(id);
      const neighbors = [];
      
      for (let i = startIdx; i < endIdx; i++) {
        cursor.moveTo(i);
        const targetId = cursor.targetId;
        neighbors.push({
          targetId: targetId,
          type: cursor.type,
        });

        if (depth < maxDepth && !visited.has(targetId)) {
          visited.add(targetId);
          queue.push({ id: targetId, depth: depth + 1 });
        }
      }
      
      flowGraph.push({
        ...nodeInfo,
        neighbors
      });
      
      // Hard limit for context window safety
      if (flowGraph.length >= 1000) break;
    }

    return flowGraph;
  }
}
