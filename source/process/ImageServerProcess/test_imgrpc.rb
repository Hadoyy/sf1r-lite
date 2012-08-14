#!/usr/bin/env ruby
#
require 'rubygems'
require 'msgpack'
require 'msgpack/rpc'

client = MessagePack::RPC::Client.new('127.0.0.1', 18821)

#msg = [false, 0, "~/workspace/sf1/sf1r-engine/source/process/ImageServerProcess/Image-f7b55a91e5d431983b8d3f179687806a.pic"];
#msg = [false, 1, "1234\000\00012346789aaaaaa\000\000dfdfjjjjjjjjjj"];
#rsp = client.call(:upload_image, msg)
#p rsp

#p client.call(:compute_image_color, [false, "./success_upload_tfs", 1]);

#p client.call( :delete_image, [false, rsp[2] ])

#p client.call(:export_image, ['~/workspace/sf1/sf1r-engine/bin/image_upload_log', '~/workspace/sf1/sf1r-engine/bin/imgout'])

