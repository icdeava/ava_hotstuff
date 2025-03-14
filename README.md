# Instructions for Running the Code

## Setup on Ubuntu-Based Systems

On an Ubuntu-based system, run the following commands:

```bash
sudo apt -y update
sudo apt -y install build-essential
sudo apt -y install autoconf libtool libssl-dev libuv1-dev cmake pkg-config cmake-data make
```

## Running the Simulation

A Jupyter Notebook script, `scripts/deploy/example_local_simulation.ipynb`, simulates a 2-cluster setup locally.

To better understand the steps involved in generating the configuration files needed to run the simulation, review the files being created or modified in `scripts/deploy/example_local_simulation.ipynb`.

## Important Scripts and Configuration Files

The script `scripts/deploy/job_mini.sh` plays a crucial role in generating the necessary configuration files:

- `hotstuff.gen.conf`
- `hotstuff.gen-sec*.conf`

To adjust key simulation parameters such as block size, modify `scripts/gen_conf.py`.

## Configuring IP Addresses

Update the following files with the appropriate IP addresses for your cloud or local instances:

- `scripts/deploy/all_internal_ips`
- `scripts/deploy/all_external_ips`

### Example: Retrieving Internal IPs on Google Cloud

Run the following command to fetch internal IPs from Google Cloud and save them to `all_internal_ips`:

```bash
gcloud compute instances list --format="value(networkInterfaces[0].networkIP)" > all_internal_ips
```

## Cluster Configuration

The `cluster_info_hs.txt` file defines the cluster number for the nodes in the system. The number of lines in this file corresponds to the number of servers (excluding clients).
