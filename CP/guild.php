<?php
	session_start();
	include_once 'config.php';
	include_once 'functions.php';

	$Rank = array(
		"Civilian",
		"Private",
		"Corporal",
		"Sergeant",
		"Master<br>Sergeant",
		"Sergeant<br>Major",
		"Knight",
		"Knight<br>Lieutenant",
		"Knight<br>Captain",
		"Knight<br>Champion",
		"Lieutenant<br>Commander",
		"Commander",
		"Marshal",
		"Field Marshal",
		"Grand Marshal",
	);

	function calc_rank($score)
	{
		$result = intval($score / 270);
		if( $result > 14 )
			$result = 14;

		return $result;
	}

	if( !isset($_GET['opt']) )
	{
		echo '
			<div class="ladder_class">
		';

		opentable('Ranking General de Guilds');

		echo '
			<table width="600">
				<tr>
					<td align="center">
						<form id="ladder">
							<select name="opt" onChange="javascript:GET_ajax(\'guild.php\', \'ladder_div\', \'ladder\');">
								<option selected value="0">Nivel y Experiencia</option>
								<option value="1">Propiedad de Castillos</option>
								<option value="2">World Conquest War</option>
							</select>
						</form>
					</td>
				</tr>
			</table>
			<div id="ladder_div" style="color:#000000">
		';

		$begin = 1;
		$_GET['opt'] = 0;
	}

	if( $_GET['opt'] == 0 )
	{ // Nivel y Experiencia
?>
	<table width="600">
		<tr>
			<td align="left" class="head">#</td>
			<td align="center" class="head">Emblema</td>
			<td align="left" class="head">Nombre</td>
			<td align="left" class="head">Nivel</td>
			<td align="right" class="head">Experiencia</td>
			<td align="right" class="head">Promedio</td>
		</tr>
<?php
		$result = $mysql->query("SELECT `name`, `emblem_data`, `guild_lv`, `exp`, `guild_id`, `average_lv` FROM `guild` WHERE `guild_id` > '1' ORDER BY `guild_lv` DESC, `exp` DESC LIMIT 25", $CONFIG['DBMain']);

		for( $i = 1; ($line = $mysql->fetcharray($result)); $i++ )
		{
			$emblems[$line['guild_id']] = $line['emblem_data'];

			echo '
				<tr>
					<td align="left">' . $i . '</td>
					<td align="center"><img src="emblema.php?data=' . $line['guild_id'] . '" alt="' . htmlformat($line['name']) . '"></td>
					<td align="left">
						<span title="Ver Profile de Guild" style="color:#000000; cursor:pointer" onMouseOver="this.style.color=#0000FF" onMouseOut="this.style.color=#000000" onClick="return LINK_ajax(\'guildprofile.php?guild_id=' . $line['guild_id'] . '\',\'main_div\');">
							' . htmlformat($line['name']) . '
						</span>
					</td>
					<td align="left">' . $line['guild_lv'] . '</td>
					<td align="right">' . moneyformat($line['exp']) . '</td>
					<td align="right">' . $line['average_lv'] . '</td>
				</tr>
			';
		}
	}
	else if( $_GET['opt'] == 1 )
	{ // WoE Castles
		$castles = $_SESSION[$CONFIG['Name'] . 'castles'];
		$oclastes = array(
			0 => "aldeg_cas01",
			1 => "aldeg_cas02",
			2 => "aldeg_cas03",
			3 => "aldeg_cas04",
			4 => "aldeg_cas05",
			5 => "gefg_cas01",
			6 => "gefg_cas02",
			7 => "gefg_cas03",
			8 => "gefg_cas04",
			9 => "gefg_cas05",
			10 => "payg_cas01",
			11 => "payg_cas02",
			12 => "payg_cas03",
			13 => "payg_cas04",
			14 => "payg_cas05",
			15 => "prtg_cas01",
			16 => "prtg_cas02",
			17 => "prtg_cas03",
			18 => "prtg_cas04",
			19 => "prtg_cas05",
			20 => "nguild_alde",
			21 => "nguild_gef",
			22 => "nguild_pay",
			23 => "nguild_prt",
			24 => "schg_cas01",
			25 => "schg_cas02",
			26 => "schg_cas03",
			27 => "schg_cas04",
			28 => "schg_cas05",
			29 => "arug_cas01",
			30 => "arug_cas02",
			31 => "arug_cas03",
			32 => "arug_cas04",
			33 => "arug_cas05"
		);

?>
	<table width="600">
		<tr>
			<td align="left" class="head">#</td>
			<td align="center" class="head">Emblema</td>
			<td align="left" class="head">Nombre</td>
			<td align="center" class="head">Castillo</td>
			<td align="center" class="head">ECO</td>
			<td align="center" class="head">DEF</td>
		</tr>
<?php
		$result = $mysql->query("
			SELECT
				`guild_castle`.`castle_id`, `guild_castle`.`economy`, `guild_castle`.`defense`,
				`guild`.`guild_id`, `guild`.`name`, `guild`.`emblem_data`
			FROM
				`guild_castle` LEFT JOIN `guild` ON `guild`.`guild_id` = `guild_castle`.`guild_id`
			WHERE
				`guild_castle`.`guild_id` > '0'
			ORDER BY
				`guild_castle`.`castle_id`
			", $CONFIG['DBMain']);

		for( $i = 1; ($line = $mysql->fetcharray($result)); $i++ )
		{
			if( isset($castles[$line['castle_id']]) )
				$cname = $castles[$line['castle_id']];
			else
				continue;

			$emblems[$line['guild_id']] = $line['emblem_data'];

			echo '
				<tr>
					<td align="left">' . $i . '</td>
					<td align="center"><img src="emblema.php?data=' . $line['guild_id'] . '" alt="' . htmlformat($line['name']) . '"></td>
					<td align="left">
						<span title="Ver Profile de Guild" style="color:#000000; cursor:pointer" onMouseOver="this.style.color=#0000FF" onMouseOut="this.style.color=#000000" onClick="return LINK_ajax(\'guildprofile.php?guild_id=' . $line['guild_id'] . '\',\'main_div\');">
							' . htmlformat($line['name']) . '
						</span>
					</td>
					<td align="center">' . $cname . '<br>' . $oclastes[$line['castle_id']] . '</td>
					<td align="center">' . $line["economy"] . '</td>
					<td align="center">' . $line["defense"] . '</td>
				</tr>
			';
		}
	}
	else /* if( $_GET['opt'] == 2 ) */
	{
		$regions = array(
			1 => 'Prontera, Capital of Rune Midgard',
			7 => 'Payon Town',
			8 => 'Morroc Town',
			9 => 'Wootan Tribe\'s Village, Umbala',
			10 => 'Beach Town, Comodo',
			11 => 'Niflheim, Realm of the Dead',
			12 => 'Aldebaran',
			13 => 'Geffen',
			2 => 'Yuno, Capital of Schwarzwald Republic',
			3 => 'Hugel, the Quaint Garden Village',
			4 => 'Lighthalzen, the City of Prosperity',
			5 => 'Einbroch, the City of Steel',
			6 => 'Einbech, the Mining Village'
		);

?>
	<table width="600">
		<tr>
			<td align="left" class="head">#</td>
			<td align="center" class="head">Emblema</td>
			<td align="left" class="head">Nombre</td>
			<td align="right" class="head">Conquested Region</td>
		</tr>
<?php
		$result = $mysql->query("
			SELECT
				`guild`.`guild_id`, `guild`.`name`, `guild`.`emblem_data`, `guild`.`guild_lv`,
				`mapreg`.`index`
			FROM
				`guild` LEFT JOIN `mapreg` ON `mapreg`.`value` = `guild`.`guild_id`
			WHERE
				`mapreg`.`varname` = '\$Region'
			ORDER BY
				`mapreg`.`index`
			", $CONFIG['DBMain']);

		for( $i = 1; ($line = $mysql->fetcharray($result)); $i++ )
		{
			$emblems[$line['guild_id']] = $line['emblem_data'];
			echo '
				<tr>
					<td align="left">' . $i . '</td>
					<td align="center"><img src="emblema.php?data=' . $line['guild_id'] . '" alt="' . htmlformat($line['name']) . '"></td>
					<td align="left">
						<span title="Ver Profile de Guild" style="color:#000000; cursor:pointer" onMouseOver="this.style.color=#0000FF" onMouseOut="this.style.color=#000000" onClick="return LINK_ajax(\'guildprofile.php?guild_id=' . $line['guild_id'] . '\',\'main_div\');">
							' . htmlformat($line['name']) . '
						</span>
					</td>
					<td align="right">' . htmlformat($regions[$line['index']]) . '</td>
				</tr>
			';
		}
	}

	if( isset($emblems) )
		$_SESSION[$CONFIG['Name'] . 'emblems'] = $emblems;

	echo "
			</table>
		</div>
	";

	if( isset($begin) )
	{
		echo "
			</div>
		";

		closetable();
	}
?>