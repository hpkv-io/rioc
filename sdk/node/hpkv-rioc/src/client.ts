/// <reference types="node" />
import { RiocConfig } from './config';
import { createError } from './errors';
import Debug from 'debug';

const debug = Debug('hpkv:rioc:client');

// Import native addon
const native = require('../build/Release/rioc.node');

/**
 * Represents a key-value pair returned from a range query.
 */
export interface RangeQueryResult {
  key: Buffer;
  value: Buffer;
}

/**
 * Main client class for interacting with RIOC.
 */
export class RiocClient {
  private client: any;
  private isDisposed = false;

  /**
   * Creates a new RIOC client.
   * @param config The client configuration.
   */
  constructor(config: RiocConfig) {
    debug('Initializing client with config:', config);
    this.client = new native.RiocClient(config);
  }

  /**
   * Gets a value by key.
   * @param key The key to get.
   * @returns The value if found, null if not found.
   */
  get(key: Buffer): Buffer | null {
    if (this.isDisposed) {
      throw new Error('Client is disposed');
    }
    return this.client.get(key);
  }

  /**
   * Inserts or updates a key-value pair.
   * @param key The key to insert.
   * @param value The value to insert.
   * @param timestamp The timestamp for the operation.
   */
  insert(key: Buffer, value: Buffer, timestamp: bigint): void {
    if (this.isDisposed) {
      throw new Error('Client is disposed');
    }
    this.client.insert(key, value, timestamp);
  }

  /**
   * Deletes a key-value pair.
   * @param key The key to delete.
   * @param timestamp The timestamp for the operation.
   */
  delete(key: Buffer, timestamp: bigint): void {
    if (this.isDisposed) {
      throw new Error('Client is disposed');
    }
    this.client.delete(key, timestamp);
  }

  /**
   * Performs a range query to retrieve all key-value pairs within the specified range.
   * @param startKey The start key of the range (inclusive).
   * @param endKey The end key of the range (inclusive).
   * @returns An array of key-value pairs within the specified range.
   */
  rangeQuery(startKey: Buffer, endKey: Buffer): RangeQueryResult[] {
    if (this.isDisposed) {
      throw new Error('Client is disposed');
    }
    return this.client.rangeQuery(startKey, endKey);
  }

  /**
   * Creates a new batch operation.
   * @returns A new batch instance.
   */
  createBatch(): RiocBatch {
    if (this.isDisposed) {
      throw new Error('Client is disposed');
    }
    return new RiocBatch(this.client.createBatch());
  }

  /**
   * Gets the current timestamp in nanoseconds.
   */
  static getTimestamp(): bigint {
    return native.RiocClient.getTimestamp();
  }

  /**
   * Atomically increments or decrements a counter value.
   * @param key The key of the counter.
   * @param value The value to add (positive) or subtract (negative).
   * @param timestamp The timestamp for the operation.
   * @returns The new value of the counter after the operation.
   */
  atomicIncDec(key: Buffer, value: number, timestamp: bigint): bigint {
    if (this.isDisposed) {
      throw new Error('Client is disposed');
    }
    return this.client.atomicIncDec(key, value, timestamp);
  }

  /**
   * Disposes the client resources.
   */
  dispose(): void {
    if (!this.isDisposed) {
      debug('Disposing client');
      this.client.dispose();
      this.isDisposed = true;
    }
  }
}

/**
 * Represents a batch of operations to be executed together.
 */
export class RiocBatch {
  private isDisposed = false;

  constructor(private batch: any) {}

  /**
   * Adds a get operation to the batch.
   * @param key The key to get.
   */
  addGet(key: Buffer): void {
    if (this.isDisposed) {
      throw new Error('Batch is disposed');
    }
    this.batch.addGet(key);
  }

  /**
   * Adds an insert operation to the batch.
   * @param key The key to insert.
   * @param value The value to insert.
   * @param timestamp The timestamp for the operation.
   */
  addInsert(key: Buffer, value: Buffer, timestamp: bigint): void {
    if (this.isDisposed) {
      throw new Error('Batch is disposed');
    }
    this.batch.addInsert(key, value, timestamp);
  }

  /**
   * Adds a delete operation to the batch.
   * @param key The key to delete.
   * @param timestamp The timestamp for the operation.
   */
  addDelete(key: Buffer, timestamp: bigint): void {
    if (this.isDisposed) {
      throw new Error('Batch is disposed');
    }
    this.batch.addDelete(key, timestamp);
  }

  /**
   * Adds a range query operation to the batch.
   * @param startKey The start key of the range (inclusive).
   * @param endKey The end key of the range (inclusive).
   */
  addRangeQuery(startKey: Buffer, endKey: Buffer): void {
    if (this.isDisposed) {
      throw new Error('Batch is disposed');
    }
    this.batch.addRangeQuery(startKey, endKey);
  }

  /**
   * Adds an atomic increment/decrement operation to the batch.
   * @param key The key of the counter.
   * @param value The value to increment (positive) or decrement (negative).
   * @param timestamp The timestamp for the operation.
   */
  addAtomicIncDec(key: Buffer, value: number, timestamp: bigint): void {
    if (this.isDisposed) {
      throw new Error('Batch is disposed');
    }
    this.batch.addAtomicIncDec(key, value, timestamp);
  }

  /**
   * Executes the batch asynchronously.
   * @returns A batch tracker for monitoring the execution.
   */
  executeAsync(): RiocBatchTracker {
    if (this.isDisposed) {
      throw new Error('Batch is disposed');
    }
    return new RiocBatchTracker(this.batch.executeAsync());
  }

  /**
   * Disposes the batch resources.
   */
  dispose(): void {
    if (!this.isDisposed) {
      debug('Disposing batch');
      this.batch.dispose();
      this.isDisposed = true;
    }
  }
}

/**
 * Tracks the execution of a batch operation.
 */
export class RiocBatchTracker {
  private isDisposed = false;

  constructor(private tracker: any) {}

  /**
   * Waits for the batch execution to complete.
   * @param timeoutMs Optional timeout in milliseconds.
   */
  wait(timeoutMs?: number): void {
    if (this.isDisposed) {
      throw new Error('Tracker is disposed');
    }
    this.tracker.wait(timeoutMs);
  }

  /**
   * Gets the response for a specific operation in the batch.
   * @param index The index of the operation.
   * @returns The response value if it's a get operation.
   */
  getResponse(index: number): Buffer | null {
    if (this.isDisposed) {
      throw new Error('Tracker is disposed');
    }
    return this.tracker.getResponse(index);
  }

  /**
   * Gets the response for a range query operation in the batch.
   * @param index The index of the operation.
   * @returns An array of key-value pairs within the specified range.
   */
  getRangeQueryResponse(index: number): RangeQueryResult[] {
    if (this.isDisposed) {
      throw new Error('Tracker is disposed');
    }
    return this.tracker.getRangeQueryResponse(index);
  }

  /**
   * Gets the result for an atomic increment/decrement operation in the batch.
   * @param index The index of the operation.
   * @returns The new value of the counter after the operation.
   */
  getAtomicResult(index: number): bigint {
    if (this.isDisposed) {
      throw new Error('Tracker is disposed');
    }
    return this.tracker.getAtomicResult(index);
  }

  /**
   * Disposes the tracker resources.
   */
  dispose(): void {
    if (!this.isDisposed) {
      debug('Disposing batch tracker');
      this.tracker.dispose();
      this.isDisposed = true;
    }
  }
}