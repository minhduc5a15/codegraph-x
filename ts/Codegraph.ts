export enum NodeType {
    FILE = 0,
    CLASS = 1,
    FUNCTION = 2,
    METHOD = 3
}

export enum EdgeType {
    CALLS = 0,
    INHERITS = 1,
    IMPORTS = 2
}

export interface GraphData {
    nodes: ArrayBuffer;
    offsets: ArrayBuffer;
    edges: ArrayBuffer;
    stringPool: ArrayBuffer;
}

export class EdgeCursor {
    private currentByteOffset: number = 0;

    constructor(private edgesView: DataView) { }

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

    constructor(data: GraphData) {
        this.nodesView = new DataView(data.nodes);
        this.offsets = new Uint32Array(data.offsets);
        this.edgesView = new DataView(data.edges);
        this.stringPool = new Uint8Array(data.stringPool);
        this.textDecoder = new TextDecoder('utf-8');
        this.sharedEdgeCursor = new EdgeCursor(this.edgesView);
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
            type: this.nodesView.getUint8(byteOffset + 20) as NodeType
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
            endIdx: this.offsets[nodeId + 1]
        };
    }

    public get nodeCount(): number {
        return this.offsets.length - 1;
    }
}
