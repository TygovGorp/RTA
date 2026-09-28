/*
 *	Fully written by Claude
*/

#include "EchogramXlsx.h"

#include "Echogram.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"

namespace
{
	uint32 ZipCrc32(const uint8* Data, int64 Size)
	{
		// Function-local static: built once, thread-safe initialisation.
		struct FTable
		{
			uint32 V[256];
			FTable()
			{
				for (uint32 i = 0; i < 256; ++i)
				{
					uint32 C = i;
					for (int32 k = 0; k < 8; ++k)
					{
						C = (C & 1u) ? (0xEDB88320u ^ (C >> 1)) : (C >> 1);
					}
					V[i] = C;
				}
			}
		};
		static const FTable Table;

		uint32 Crc = 0xFFFFFFFFu;
		for (int64 i = 0; i < Size; ++i)
		{
			Crc = Table.V[(Crc ^ Data[i]) & 0xFFu] ^ (Crc >> 8);
		}
		return Crc ^ 0xFFFFFFFFu;
	}

	void Put16(TArray<uint8>& Out, uint32 V)
	{
		Out.Add(uint8(V & 0xFF));
		Out.Add(uint8((V >> 8) & 0xFF));
	}

	void Put32(TArray<uint8>& Out, uint32 V)
	{
		Out.Add(uint8(V & 0xFF));
		Out.Add(uint8((V >> 8) & 0xFF));
		Out.Add(uint8((V >> 16) & 0xFF));
		Out.Add(uint8((V >> 24) & 0xFF));
	}

	class FStoredZipWriter
	{
	public:
		FStoredZipWriter()
		{
			const FDateTime Now = FDateTime::Now();
			DosTime = uint16((Now.GetHour() << 11) | (Now.GetMinute() << 5) | (Now.GetSecond() / 2));
			DosDate = uint16(((FMath::Max(Now.GetYear(), 1980) - 1980) << 9) | (Now.GetMonth() << 5) | Now.GetDay());
		}

		/** Name must be an ASCII literal; it is stored by pointer. */
		void AddFile(const ANSICHAR* Name, const FString& Contents)
		{
			FTCHARToUTF8 Utf8(*Contents);
			const uint8* Bytes = reinterpret_cast<const uint8*>(Utf8.Get());
			const uint32 Size = uint32(Utf8.Length());
			const uint32 NameLen = uint32(FCStringAnsi::Strlen(Name));

			FEntry& Entry = Entries.AddDefaulted_GetRef();
			Entry.Name = Name;
			Entry.Crc = ZipCrc32(Bytes, Size);
			Entry.Size = Size;
			Entry.Offset = uint32(Buffer.Num());

			Put32(Buffer, 0x04034B50);   // local file header signature
			Put16(Buffer, 20);           // version needed (2.0)
			Put16(Buffer, 0);            // flags
			Put16(Buffer, 0);            // method: stored
			Put16(Buffer, DosTime);
			Put16(Buffer, DosDate);
			Put32(Buffer, Entry.Crc);
			Put32(Buffer, Size);         // compressed size == uncompressed for stored
			Put32(Buffer, Size);
			Put16(Buffer, NameLen);
			Put16(Buffer, 0);            // extra field length
			Buffer.Append(reinterpret_cast<const uint8*>(Name), NameLen);
			Buffer.Append(Bytes, Size);
		}

		const TArray<uint8>& Finish()
		{
			const uint32 CentralStart = uint32(Buffer.Num());
			for (const FEntry& Entry : Entries)
			{
				const uint32 NameLen = uint32(FCStringAnsi::Strlen(Entry.Name));
				Put32(Buffer, 0x02014B50);   // central directory header signature
				Put16(Buffer, 20);           // version made by
				Put16(Buffer, 20);           // version needed
				Put16(Buffer, 0);            // flags
				Put16(Buffer, 0);            // method: stored
				Put16(Buffer, DosTime);
				Put16(Buffer, DosDate);
				Put32(Buffer, Entry.Crc);
				Put32(Buffer, Entry.Size);
				Put32(Buffer, Entry.Size);
				Put16(Buffer, NameLen);
				Put16(Buffer, 0);            // extra
				Put16(Buffer, 0);            // comment
				Put16(Buffer, 0);            // disk number
				Put16(Buffer, 0);            // internal attributes
				Put32(Buffer, 0);            // external attributes
				Put32(Buffer, Entry.Offset);
				Buffer.Append(reinterpret_cast<const uint8*>(Entry.Name), NameLen);
			}
			const uint32 CentralSize = uint32(Buffer.Num()) - CentralStart;

			Put32(Buffer, 0x06054B50);       // end of central directory
			Put16(Buffer, 0);
			Put16(Buffer, 0);
			Put16(Buffer, uint32(Entries.Num()));
			Put16(Buffer, uint32(Entries.Num()));
			Put32(Buffer, CentralSize);
			Put32(Buffer, CentralStart);
			Put16(Buffer, 0);                // comment length
			return Buffer;
		}

	private:
		struct FEntry
		{
			const ANSICHAR* Name = nullptr;
			uint32 Crc = 0;
			uint32 Size = 0;
			uint32 Offset = 0;
		};

		TArray<FEntry> Entries;
		TArray<uint8> Buffer;
		uint16 DosTime = 0;
		uint16 DosDate = 0;
	};

	// =====================================================================
	// SpreadsheetML helpers
	// =====================================================================

	/** Column letters for A..Z. The widest sheet here uses 2 + NumBands columns. */
	const TCHAR* ColumnName(int32 Index)
	{
		static const TCHAR* Names[] = {
			TEXT("A"), TEXT("B"), TEXT("C"), TEXT("D"), TEXT("E"), TEXT("F"), TEXT("G"),
			TEXT("H"), TEXT("I"), TEXT("J"), TEXT("K"), TEXT("L"), TEXT("M"), TEXT("N"),
			TEXT("O"), TEXT("P"), TEXT("Q"), TEXT("R"), TEXT("S"), TEXT("T"), TEXT("U"),
			TEXT("V"), TEXT("W"), TEXT("X"), TEXT("Y"), TEXT("Z") };
		check(Index >= 0 && Index < int32(UE_ARRAY_COUNT(Names)));
		return Names[Index];
	}

	FString EscapeXml(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len() + 16);
		for (const TCHAR C : In)
		{
			switch (C)
			{
			case TEXT('&'):  Out += TEXT("&amp;");  break;
			case TEXT('<'):  Out += TEXT("&lt;");   break;
			case TEXT('>'):  Out += TEXT("&gt;");   break;
			case TEXT('"'):  Out += TEXT("&quot;"); break;
			default:         Out.AppendChar(C);     break;
			}
		}
		return Out;
	}

	void AppendStringCell(FString& Xml, int32 Col, int32 Row, const FString& Text, int32 Style = 0)
	{
		Xml += FString::Printf(TEXT("<c r=\"%s%d\" s=\"%d\" t=\"inlineStr\"><is><t xml:space=\"preserve\">%s</t></is></c>"),
			ColumnName(Col), Row, Style, *EscapeXml(Text));
	}

	// Style indices into cellXfs in styles.xml below.
	constexpr int32 StyleDefault = 0;
	constexpr int32 StyleBold    = 1;
	constexpr int32 StyleInput   = 2;   // blue: the one cell meant to be edited
	constexpr int32 StyleTitle   = 3;
	constexpr int32 StyleEnergy  = 4;   // 0.000E+00
	constexpr int32 StyleDb      = 5;   // 0.00
	constexpr int32 StyleTime    = 6;   // 0.0

	// Chart y-axis bottom and the default of the editable floor cell.
	constexpr float DefaultFloorDb = -80.f;

	double LinearToDb(double Energy, double Peak, double FloorDb)
	{
		if (Peak <= 0.0) return FloorDb;
		const double Ratio = FMath::Max(Energy, 1e-30) / Peak;
		return FMath::Max(FloorDb, 10.0 * FMath::Loge(Ratio) / FMath::Loge(10.0));
	}

	// =====================================================================
	// Static parts
	// =====================================================================

	const TCHAR* ContentTypesXml = TEXT(R"xml(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>
<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>
<Override PartName="/xl/worksheets/sheet2.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>
<Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/>
<Override PartName="/xl/drawings/drawing1.xml" ContentType="application/vnd.openxmlformats-officedocument.drawing+xml"/>
<Override PartName="/xl/charts/chart1.xml" ContentType="application/vnd.openxmlformats-officedocument.drawingml.chart+xml"/>
</Types>)xml");

	const TCHAR* RootRelsXml = TEXT(R"xml(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>
</Relationships>)xml");

	// activeTab=1 opens on the chart. fullCalcOnLoad makes Excel recompute
	// rather than trust the cached values written below.
	const TCHAR* WorkbookXml = TEXT(R"xml(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
<bookViews><workbookView activeTab="1"/></bookViews>
<sheets>
<sheet name="Raw" sheetId="1" r:id="rId1"/>
<sheet name="Echogram Chart" sheetId="2" r:id="rId2"/>
</sheets>
<calcPr calcId="191029" fullCalcOnLoad="1"/>
</workbook>)xml");

	const TCHAR* WorkbookRelsXml = TEXT(R"xml(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>
<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet2.xml"/>
<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>
</Relationships>)xml");

	const TCHAR* StylesXml = TEXT(R"xml(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
<numFmts count="2"><numFmt numFmtId="164" formatCode="0.000E+00"/><numFmt numFmtId="165" formatCode="0.0"/></numFmts>
<fonts count="4">
<font><sz val="10"/><name val="Arial"/><family val="2"/></font>
<font><b/><sz val="10"/><name val="Arial"/><family val="2"/></font>
<font><sz val="10"/><color rgb="FF0000FF"/><name val="Arial"/><family val="2"/></font>
<font><b/><sz val="14"/><name val="Arial"/><family val="2"/></font>
</fonts>
<fills count="3"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill><fill><patternFill patternType="solid"><fgColor rgb="FFFFFF00"/><bgColor indexed="64"/></patternFill></fill></fills>
<borders count="1"><border><left/><right/><top/><bottom/><diagonal/></border></borders>
<cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>
<cellXfs count="7">
<xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/>
<xf numFmtId="0" fontId="1" fillId="0" borderId="0" xfId="0" applyFont="1"/>
<xf numFmtId="0" fontId="2" fillId="2" borderId="0" xfId="0" applyFont="1" applyFill="1"/>
<xf numFmtId="0" fontId="3" fillId="0" borderId="0" xfId="0" applyFont="1"/>
<xf numFmtId="164" fontId="0" fillId="0" borderId="0" xfId="0" applyNumberFormat="1"/>
<xf numFmtId="2" fontId="0" fillId="0" borderId="0" xfId="0" applyNumberFormat="1"/>
<xf numFmtId="165" fontId="0" fillId="0" borderId="0" xfId="0" applyNumberFormat="1"/>
</cellXfs>
<cellStyles count="1"><cellStyle name="Normal" xfId="0" builtinId="0"/></cellStyles>
</styleSheet>)xml");

	const TCHAR* ChartSheetRelsXml = TEXT(R"xml(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing" Target="../drawings/drawing1.xml"/>
</Relationships>)xml");

	// Chart sits right of the data columns and below the header notes (rows 1-6),
	// whose long text would otherwise spill underneath it.
	const TCHAR* DrawingXml = TEXT(R"xml(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xdr:wsDr xmlns:xdr="http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing" xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main">
<xdr:twoCellAnchor>
<xdr:from><xdr:col>8</xdr:col><xdr:colOff>0</xdr:colOff><xdr:row>7</xdr:row><xdr:rowOff>0</xdr:rowOff></xdr:from>
<xdr:to><xdr:col>23</xdr:col><xdr:colOff>0</xdr:colOff><xdr:row>38</xdr:row><xdr:rowOff>0</xdr:rowOff></xdr:to>
<xdr:graphicFrame macro="">
<xdr:nvGraphicFramePr><xdr:cNvPr id="2" name="Echogram"/><xdr:cNvGraphicFramePr/></xdr:nvGraphicFramePr>
<xdr:xfrm><a:off x="0" y="0"/><a:ext cx="0" cy="0"/></xdr:xfrm>
<a:graphic><a:graphicData uri="http://schemas.openxmlformats.org/drawingml/2006/chart"><c:chart xmlns:c="http://schemas.openxmlformats.org/drawingml/2006/chart" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" r:id="rId1"/></a:graphicData></a:graphic>
</xdr:graphicFrame>
<xdr:clientData/>
</xdr:twoCellAnchor>
</xdr:wsDr>)xml");

	const TCHAR* DrawingRelsXml = TEXT(R"xml(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart" Target="../charts/chart1.xml"/>
</Relationships>)xml");

	// =====================================================================
	// Generated parts
	// =====================================================================

	FString BuildRawSheet(const FEchogram& Echogram, int32& OutNonFinite)
	{
		FString Xml;
		Xml.Reserve(Echogram.GetNumBins() * (48 + RTA::NumBands * 40));
		Xml += TEXT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
			"<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
			"<sheetViews><sheetView workbookViewId=\"0\"><pane ySplit=\"1\" topLeftCell=\"A2\" activePane=\"bottomLeft\" state=\"frozen\"/></sheetView></sheetViews>"
			"<cols><col min=\"1\" max=\"8\" width=\"14\" customWidth=\"1\"/></cols><sheetData>");

		Xml += TEXT("<row r=\"1\">");
		AppendStringCell(Xml, 0, 1, TEXT("BinIndex"), StyleBold);
		AppendStringCell(Xml, 1, 1, TEXT("TimeMs"), StyleBold);
		for (int32 Band = 0; Band < RTA::NumBands; ++Band)
		{
			AppendStringCell(Xml, 2 + Band, 1, FString::Printf(TEXT("%.0fHz"), RTA::FrequencyBands[Band]), StyleBold);
		}
		Xml += TEXT("</row>");

		for (int32 Bin = 0; Bin < Echogram.GetNumBins(); ++Bin)
		{
			const int32 Row = Bin + 2;
			Xml += FString::Printf(TEXT("<row r=\"%d\"><c r=\"A%d\"><v>%d</v></c><c r=\"B%d\" s=\"%d\"><v>%.6g</v></c>"),
				Row, Row, Bin, Row, StyleTime, Bin * Echogram.GetBinWidthSeconds() * 1000.0);

			for (int32 Band = 0; Band < RTA::NumBands; ++Band)
			{
				float Value = Echogram.At(Band, Bin);
				if (!FMath::IsFinite(Value))
				{
					Value = 0.f;
					++OutNonFinite;
				}
				Xml += FString::Printf(TEXT("<c r=\"%s%d\" s=\"%d\"><v>%.9g</v></c>"),
					ColumnName(2 + Band), Row, StyleEnergy, Value);
			}
			Xml += TEXT("</row>");
		}

		Xml += TEXT("</sheetData></worksheet>");
		return Xml;
	}

	FString BuildChartSheet(const FEchogram& Echogram, const FString& Note)
	{
		const int32 FirstRawRow = 2;
		const int32 LastRawRow  = Echogram.GetNumBins() + 1;
		const int32 HeaderRow   = 7;
		const int32 FirstRow    = HeaderRow + 1;
		const FString LastBandCol = ColumnName(1 + RTA::NumBands);   // on Raw: C..(C+NumBands-1)

		// Cached peak, mirroring the formula in B3.
		double Peak = 0.0;
		
		for (const float V : Echogram)
		{
			if (FMath::IsFinite(V)) Peak = FMath::Max(Peak, double(V));
		}

		FString Xml;
		Xml.Reserve( Echogram.GetNumBins() * (80 + RTA::NumBands * 120));
		Xml += TEXT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
			"<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
			"xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
			"<sheetViews><sheetView tabSelected=\"1\" workbookViewId=\"0\"/></sheetViews>"
			"<cols><col min=\"1\" max=\"1\" width=\"16\" customWidth=\"1\"/>"
			"<col min=\"2\" max=\"7\" width=\"13\" customWidth=\"1\"/></cols><sheetData>");

		// ---- Header block (rows 1-6) ------------------------------------
		Xml += TEXT("<row r=\"1\">");
		AppendStringCell(Xml, 0, 1, TEXT("Echogram - energy decay per octave band, normalised to overall peak"), StyleTitle);
		Xml += TEXT("</row><row r=\"2\">");
		AppendStringCell(Xml, 0, 2, FString::Printf(TEXT("Source: 'Raw' sheet (linear energy). %s"), *Note));
		Xml += TEXT("</row><row r=\"3\">");
		AppendStringCell(Xml, 0, 3, TEXT("Peak reference (max of all bands, linear):"));
		Xml += FString::Printf(TEXT("<c r=\"B3\" s=\"%d\"><f>MAX(Raw!C%d:%s%d)</f><v>%.9g</v></c>"),
			StyleEnergy, FirstRawRow, *LastBandCol, LastRawRow, Peak);
		Xml += TEXT("</row><row r=\"4\">");
		AppendStringCell(Xml, 0, 4, TEXT("Display floor (dB) - edit to change the plotted depth:"));
		Xml += FString::Printf(TEXT("<c r=\"B4\" s=\"%d\"><v>%.1f</v></c>"), StyleInput, DefaultFloorDb);
		Xml += TEXT("</row><row r=\"5\">");
		AppendStringCell(Xml, 0, 5, TEXT("dB = 10*LOG10(energy / peak), clipped at the floor. Zero bins sit on the floor. An empty echogram (peak 0) plots flat at the floor."));
		Xml += TEXT("</row><row r=\"6\">");
		AppendStringCell(Xml, 0, 6, TEXT("Raw values are ENERGY (the tracer bins energy, not pressure), hence 10*LOG10 rather than 20*LOG10. Chart y-axis minimum is fixed at -80; adjust it by hand if you change B4."));
		Xml += TEXT("</row>");

		// ---- Column headers (row 7) --------------------------------------
		Xml += FString::Printf(TEXT("<row r=\"%d\">"), HeaderRow);
		AppendStringCell(Xml, 0, HeaderRow, TEXT("Time (ms)"), StyleBold);
		for (int32 Band = 0; Band < RTA::NumBands; ++Band)
		{
			AppendStringCell(Xml, 1 + Band, HeaderRow,
				FString::Printf(TEXT("%.0fHz (dB)"), RTA::FrequencyBands[Band]), StyleBold);
		}
		Xml += TEXT("</row>");

		// ---- Data (formula + cached value) --------------------------------
		for (int32 Bin = 0; Bin < Echogram.GetNumBins(); ++Bin)
		{
			const int32 Row = FirstRow + Bin;
			const int32 RawRow = FirstRawRow + Bin;

			Xml += FString::Printf(TEXT("<row r=\"%d\"><c r=\"A%d\" s=\"%d\"><f>Raw!B%d</f><v>%.6g</v></c>"),
				Row, Row, StyleTime, RawRow, Bin *  Echogram.GetBinWidthSeconds() * 1000.0);

			for (int32 Band = 0; Band < RTA::NumBands; ++Band)
			{
				const float V = Echogram.At(Band, Bin);
				const double Cached = LinearToDb(FMath::IsFinite(V) ? V : 0.0, Peak, DefaultFloorDb);
				Xml += FString::Printf(
					TEXT("<c r=\"%s%d\" s=\"%d\"><f>IF($B$3&gt;0,MAX($B$4,10*LOG10(MAX(Raw!%s%d,1E-30)/$B$3)),$B$4)</f><v>%.4f</v></c>"),
					ColumnName(1 + Band), Row, StyleDb, ColumnName(2 + Band), RawRow, Cached);
			}
			Xml += TEXT("</row>");
		}

		Xml += TEXT("</sheetData><drawing r:id=\"rId1\"/></worksheet>");
		return Xml;
	}

	FString BuildChart(const FEchogram& Echogram)
	{
		const int32 FirstRow = 8;
		const int32 LastRow  = FirstRow + Echogram.GetNumBins() - 1;
		const double MaxTimeMs = Echogram.GetNumBins() * Echogram.GetBinWidthSeconds() * 1000.0;

		static const TCHAR* Colours[] = {
			TEXT("1F4E79"), TEXT("2E75B6"), TEXT("548235"),
			TEXT("BF8F00"), TEXT("C55A11"), TEXT("C00000") };

		auto RichText = [](const TCHAR* Text, int32 Size, bool bVertical)
		{
			return FString::Printf(TEXT(
				"<c:tx><c:rich><a:bodyPr%s/><a:lstStyle/><a:p><a:pPr><a:defRPr sz=\"%d\" b=\"1\"/></a:pPr>"
				"<a:r><a:rPr lang=\"en-GB\" sz=\"%d\" b=\"1\"/><a:t>%s</a:t></a:r></a:p></c:rich></c:tx><c:overlay val=\"0\"/>"),
				bVertical ? TEXT(" rot=\"-5400000\" vert=\"horz\"") : TEXT(""), Size, Size, Text);
		};

		const TCHAR* GridLine = TEXT("<c:majorGridlines><c:spPr><a:ln w=\"9360\"><a:solidFill><a:srgbClr val=\"D9D9D9\"/></a:solidFill></a:ln></c:spPr></c:majorGridlines>");

		FString Xml;
		Xml += TEXT("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
			"<c:chartSpace xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" "
			"xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
			"xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
			"<c:roundedCorners val=\"0\"/><c:chart><c:title>");
		Xml += RichText(TEXT("Echogram - octave-band energy decay"), 1400, false);
		Xml += TEXT("</c:title><c:autoTitleDeleted val=\"0\"/><c:plotArea><c:layout/>"
			"<c:scatterChart><c:scatterStyle val=\"lineMarker\"/><c:varyColors val=\"0\"/>");

		for (int32 Band = 0; Band < RTA::NumBands; ++Band)
		{
			const TCHAR* Col = ColumnName(1 + Band);
			Xml += FString::Printf(TEXT(
				"<c:ser><c:idx val=\"%d\"/><c:order val=\"%d\"/>"
				"<c:tx><c:strRef><c:f>'Echogram Chart'!$%s$7</c:f></c:strRef></c:tx>"
				"<c:spPr><a:ln w=\"9360\"><a:solidFill><a:srgbClr val=\"%s\"/></a:solidFill></a:ln></c:spPr>"
				"<c:marker><c:symbol val=\"none\"/></c:marker>"
				"<c:xVal><c:numRef><c:f>'Echogram Chart'!$A$%d:$A$%d</c:f></c:numRef></c:xVal>"
				"<c:yVal><c:numRef><c:f>'Echogram Chart'!$%s$%d:$%s$%d</c:f></c:numRef></c:yVal>"
				"<c:smooth val=\"0\"/></c:ser>"),
				Band, Band, Col, Colours[Band % int32(UE_ARRAY_COUNT(Colours))],
				FirstRow, LastRow, Col, FirstRow, Col, LastRow);
		}

		Xml += TEXT("<c:axId val=\"1001\"/><c:axId val=\"1002\"/></c:scatterChart>");

		// X axis: crosses the Y axis at its MINIMUM so it sits at the bottom.
		// With autoZero it would be drawn at 0 dB, i.e. along the top of the plot.
		Xml += FString::Printf(TEXT(
			"<c:valAx><c:axId val=\"1001\"/><c:scaling><c:orientation val=\"minMax\"/><c:max val=\"%.0f\"/><c:min val=\"0\"/></c:scaling>"
			"<c:delete val=\"0\"/><c:axPos val=\"b\"/>%s<c:title>%s</c:title>"
			"<c:numFmt formatCode=\"General\" sourceLinked=\"0\"/><c:majorTickMark val=\"out\"/><c:minorTickMark val=\"none\"/>"
			"<c:tickLblPos val=\"low\"/><c:crossAx val=\"1002\"/><c:crosses val=\"min\"/><c:crossBetween val=\"midCat\"/></c:valAx>"),
			MaxTimeMs, GridLine, *RichText(TEXT("Time (ms)"), 1000, false));

		Xml += FString::Printf(TEXT(
			"<c:valAx><c:axId val=\"1002\"/><c:scaling><c:orientation val=\"minMax\"/><c:max val=\"5\"/><c:min val=\"%.0f\"/></c:scaling>"
			"<c:delete val=\"0\"/><c:axPos val=\"l\"/>%s<c:title>%s</c:title>"
			"<c:numFmt formatCode=\"General\" sourceLinked=\"0\"/><c:majorTickMark val=\"out\"/><c:minorTickMark val=\"none\"/>"
			"<c:tickLblPos val=\"nextTo\"/><c:crossAx val=\"1001\"/><c:crosses val=\"autoZero\"/><c:crossBetween val=\"midCat\"/><c:majorUnit val=\"10\"/></c:valAx>"),
			DefaultFloorDb, GridLine, *RichText(TEXT("Level re. peak (dB)"), 1000, true));

		Xml += TEXT("</c:plotArea><c:legend><c:legendPos val=\"r\"/><c:overlay val=\"0\"/></c:legend>"
			"<c:plotVisOnly val=\"1\"/><c:dispBlanksAs val=\"gap\"/></c:chart>"
			"<c:txPr><a:bodyPr/><a:lstStyle/><a:p><a:pPr><a:defRPr sz=\"1000\"><a:latin typeface=\"Arial\"/></a:defRPr></a:pPr><a:endParaRPr lang=\"en-GB\"/></a:p></c:txPr>"
			"</c:chartSpace>");
		return Xml;
	}
}

// =========================================================================

bool RTA::WriteEchogramXlsx(const FString& Path, const FEchogram& Echogram,
                            const FString& Note, int32& OutNonFiniteCount)
{
	OutNonFiniteCount = 0;

	FStoredZipWriter Zip;
	Zip.AddFile("[Content_Types].xml",             FString(ContentTypesXml));
	Zip.AddFile("_rels/.rels",                     FString(RootRelsXml));
	Zip.AddFile("xl/workbook.xml",                 FString(WorkbookXml));
	Zip.AddFile("xl/_rels/workbook.xml.rels",      FString(WorkbookRelsXml));
	Zip.AddFile("xl/styles.xml",                   FString(StylesXml));
	Zip.AddFile("xl/worksheets/sheet1.xml",        BuildRawSheet(Echogram, OutNonFiniteCount));
	Zip.AddFile("xl/worksheets/sheet2.xml",        BuildChartSheet(Echogram, Note));
	Zip.AddFile("xl/worksheets/_rels/sheet2.xml.rels", FString(ChartSheetRelsXml));
	Zip.AddFile("xl/drawings/drawing1.xml",        FString(DrawingXml));
	Zip.AddFile("xl/drawings/_rels/drawing1.xml.rels", FString(DrawingRelsXml));
	Zip.AddFile("xl/charts/chart1.xml",            BuildChart(Echogram));

	return FFileHelper::SaveArrayToFile(Zip.Finish(), *Path);
}