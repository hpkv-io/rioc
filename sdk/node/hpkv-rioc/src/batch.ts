import { RiocBatchTracker } from './client';
import Debug from 'debug';

const debug = Debug('hpkv:rioc:batch');

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