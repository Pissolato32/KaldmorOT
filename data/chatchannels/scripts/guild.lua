function onJoin(player)
	local guild = player:getGuild()
	if guild then
		local motd = guild:getMotd()
		if motd ~= "" then
			local playerId = player:getId()
			addEvent(function()
				local p = Player(playerId)
				if p then
					p:sendChannelMessage("Message of the Day", motd, TALKTYPE_CHANNEL_R1, CHANNEL_GUILD)
				end
			end, 150)
		end
	end
	return true
end
