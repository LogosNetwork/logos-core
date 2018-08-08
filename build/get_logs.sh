#!/usr/bin/env bash

CLUSTER_NAME=$1
if [[ -z ${CLUSTER_NAME} ]]; then
    echo "Must supply a cluster name."
    exit 1
fi
# make inspection directory (incrementing inspection count) (ONE NODE)
INSTANCE_ID=$(aws ec2 describe-tags --filters "Name=resource-type,Values=instance" "Name=tag:Name,Values=$CLUSTER_NAME" --query "Tags[0].ResourceId" --output text)
aws ssm send-command --document-name "AWS-RunShellScript" --instance-ids "$INSTANCE_ID"  --parameters file://./logs_dir_management.json --timeout-seconds 600 --output-s3-bucket-name "logos-bench-command-log" --region us-east-1

# get private IP address & make corresponding dir (ALL NODES)
# move logs from LogosTest dir to EFS (erasing old logs)

aws ssm send-command --document-name "AWS-RunShellScript" --targets '{"Key":"tag:aws:cloudformation:stack-name","Values":["'"$CLUSTER_NAME"'"]}' --max-concurrency "100%" --parameters file://./helper_get_logs.json --timeout-seconds 600 --output-s3-bucket-name "logos-bench-command-log" --region us-east-1
