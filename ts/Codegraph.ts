export enum NodeType {
  FILE = 0,
  CLASS = 1,
  FUNCTION = 2,
  METHOD = 3,
  EXTERNAL = 4,
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
  private tokenIndex: Map<string, number[]> = new Map();

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

        const tokens = this.tokenizeSymbol(name);
        for (const token of tokens) {
          let tArr = this.tokenIndex.get(token);
          if (!tArr) {
            tArr = [];
            this.tokenIndex.set(token, tArr);
          }
          if (tArr.length === 0 || tArr[tArr.length - 1] !== i) {
            tArr.push(i);
          }
        }
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

  private tokenizeSymbol(name: string): string[] {
    const step1 = name.replace(/[^a-zA-Z0-9]/g, ' ');
    const step2 = step1.replace(/([a-z])([A-Z])/g, '$1 $2');
    const tokens = step2.toLowerCase().split(/\s+/).filter(t => t.length > 0);
    return Array.from(new Set(tokens));
  }

  private intersectSorted(arr1: number[], arr2: number[]): number[] {
    const result: number[] = [];
    let i = 0, j = 0;
    while(i < arr1.length && j < arr2.length) {
      if (arr1[i] < arr2[j]) i++;
      else if (arr1[i] > arr2[j]) j++;
      else {
        result.push(arr1[i]);
        i++;
        j++;
      }
    }
    return result;
  }

  private getTokenMatches(token: string): number[] {
    const exactIds = this.tokenIndex.get(token);
    if (exactIds) {
      return exactIds;
    }

    if (token.length < 2) {
      return [];
    }
      
    // Fallback: substring scan over unique tokens
    const matchedIds = new Set<number>();
    for (const [key, ids] of this.tokenIndex.entries()) {
      if (key.includes(token)) {
        for (const id of ids) {
          matchedIds.add(id);
        }
      }
    }
    
    const result = Array.from(matchedIds);
    result.sort((a, b) => a - b);
    return result;
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
    
    // Fast token index fallback with partial match support
    const tokens = this.tokenizeSymbol(nameMatch);
    if (tokens.length > 0) {
      let candidateIds = this.getTokenMatches(tokens[0]);
      for (let i = 1; i < tokens.length; i++) {
        const nextIds = this.getTokenMatches(tokens[i]);
        candidateIds = this.intersectSorted(candidateIds, nextIds);
        if (candidateIds.length === 0) break;
      }
      
      for (const id of candidateIds) {
        results.push(this.getNode(id));
        if (results.length >= 100) break;
      }
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
        // Fast token index fallback with partial match support
        const tokens = this.tokenizeSymbol(sym);
        if (tokens.length > 0) {
          let candidateIds = this.getTokenMatches(tokens[0]);
          for (let i = 1; i < tokens.length; i++) {
            const nextIds = this.getTokenMatches(tokens[i]);
            candidateIds = this.intersectSorted(candidateIds, nextIds);
            if (candidateIds.length === 0) break;
          }
          
          let addedCount = 0;
          for (const id of candidateIds) {
            if (!visited.has(id)) {
              visited.add(id);
              queue.push({ id, depth: 0 });
              addedCount++;
              if (addedCount >= 50) break; // Guard rail for starting nodes
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
        let targetName = "Unknown";
        try {
          targetName = this.getNode(targetId).name;
        } catch (e) {}

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
