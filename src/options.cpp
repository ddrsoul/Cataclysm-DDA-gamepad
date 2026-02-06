std::string options_manager::show( bool ingame, const bool world_options_only, bool with_tabs )
{
    const int iWorldOptPage = std::find_if( pages_.begin(), pages_.end(), [&]( const Page & p ) {
        return p.id_ == "world_default";
    } ) - pages_.begin();

    // temporary alias so the code below does not need to be changed
    options_container &OPTIONS = options;
    options_container &ACTIVE_WORLD_OPTIONS = world_options.has_value() ?
            *world_options.value() :
            OPTIONS;

    options_container OPTIONS_OLD = OPTIONS;
    options_container WOPTIONS_OLD = ACTIVE_WORLD_OPTIONS;
    if( world_generator->active_world == nullptr ) {
        ingame = false;
    }

    size_t sel_worldgen_tab = 1;
    std::map<size_t, inclusive_rectangle<point>> worldgen_tab_map;
    std::map<int, inclusive_rectangle<point>> opt_tab_map;
    std::map<int, inclusive_rectangle<point>> opt_line_map;
    std::set<int> vert_lines;
    vert_lines.insert( 4 );
    vert_lines.insert( 60 );

    int iCurrentPage = world_options_only ? iWorldOptPage : 0;
    int iCurrentLine = 0;
    int iStartPos = 0;

    std::unordered_map<std::string, bool> groups_state;
    groups_state.emplace( "", true ); // Non-existent group
    for( const Group &g : groups_ ) {
        // Start collapsed
        groups_state.emplace( g.id_, false );
    }

    input_context ctxt( "OPTIONS" );
    ctxt.register_cardinal();
    ctxt.register_action( "PAGE_UP", to_translation( "Fast scroll up" ) );
    ctxt.register_action( "PAGE_DOWN", to_translation( "Fast scroll down" ) );
    ctxt.register_action( "QUIT" );
    if( with_tabs || !world_options_only ) {
        ctxt.register_action( "NEXT_TAB" );
        ctxt.register_action( "PREV_TAB" );
    }
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    // for mouse selection
    ctxt.register_action( "SELECT" );
    ctxt.register_action( "MOUSE_MOVE" );
    ctxt.register_action( "SCROLL_UP" );
    ctxt.register_action( "SCROLL_DOWN" );

    const int iWorldOffset = world_options_only ? 2 : 0;
    int iTooltipHeight = 7;
    
    // Фиксированные размеры окна: 80 символов в ширину, 30 в высоту
    const int FIXED_WINDOW_WIDTH = 80;
    const int FIXED_WINDOW_HEIGHT = 30;
    
    int iContentHeight = FIXED_WINDOW_HEIGHT;
    bool recalc_startpos = false;

    catacurses::window w_options_border;
    catacurses::window w_options_tooltip;
    catacurses::window w_options_header;
    catacurses::window w_options;

    const auto init_windows = [&]( ui_adaptor & ui ) {
        recalc_startpos = true;
        if( OPTIONS.find( "TERMINAL_X" ) != OPTIONS.end() ) {
            if( OPTIONS_OLD.find( "TERMINAL_X" ) != OPTIONS_OLD.end() ) {
                OPTIONS_OLD["TERMINAL_X"] = OPTIONS["TERMINAL_X"];
            }
            if( WOPTIONS_OLD.find( "TERMINAL_X" ) != WOPTIONS_OLD.end() ) {
                WOPTIONS_OLD["TERMINAL_X"] = OPTIONS["TERMINAL_X"];
            }
        }
        if( OPTIONS.find( "TERMINAL_Y" ) != OPTIONS.end() ) {
            if( OPTIONS_OLD.find( "TERMINAL_Y" ) != OPTIONS_OLD.end() ) {
                OPTIONS_OLD["TERMINAL_Y"] = OPTIONS["TERMINAL_Y"];
            }
            if( WOPTIONS_OLD.find( "TERMINAL_Y" ) != WOPTIONS_OLD.end() ) {
                WOPTIONS_OLD["TERMINAL_Y"] = OPTIONS["TERMINAL_Y"];
            }
        }

        // Используем фиксированные размеры вместо адаптивных
        int win_width = FIXED_WINDOW_WIDTH;
        int win_height = FIXED_WINDOW_HEIGHT;
        
        // Общая высота всех элементов
        int total_height = win_height + iTooltipHeight + 3 + iWorldOffset;
        
        // Позиционируем от левого края (x=0)
        int iOffsetX = 0;
        // Центрируем по вертикали
        int iOffsetY = std::max(0, (TERMY - total_height) / 2);

        w_options_border  = catacurses::newwin( total_height, win_width,
                                                point( iOffsetX, iOffsetY ) );
        w_options_tooltip = catacurses::newwin( iTooltipHeight, win_width - 2,
                                                point( 1 + iOffsetX, iOffsetY + 1 + iWorldOffset ) );
        w_options_header  = catacurses::newwin( 1, win_width - 2,
                                                point( 1 + iOffsetX, iOffsetY + 1 + iTooltipHeight + iWorldOffset ) );
        w_options         = catacurses::newwin( iContentHeight, win_width - 2,
                                                point( 1 + iOffsetX, iOffsetY + iTooltipHeight + 2 + iWorldOffset ) );

        ui.position_from_window( w_options_border );
    };

    ui_adaptor ui;
    ui.on_screen_resize( init_windows );
    init_windows( ui );
    ui.on_redraw( [&]( const ui_adaptor & ) {
        werase( w_options_header );
        werase( w_options_border );
        werase( w_options_tooltip );
        werase( w_options );

        opt_line_map.clear();
        opt_tab_map.clear();
        if( world_options_only ) {
            if( with_tabs ) {
                worldgen_tab_map = worldfactory::draw_worldgen_tabs( w_options_border, sel_worldgen_tab );
            } else {
                werase( w_options_border );
                draw_border( w_options_border );
            }
        }

        draw_borders_external( w_options_border, iTooltipHeight + 1 + iWorldOffset, vert_lines,
                               world_options_only );
        draw_borders_internal( w_options_header, vert_lines );

        options_manager::options_container &cOPTIONS = ( ingame || world_options_only ) &&
                iCurrentPage == iWorldOptPage ?
                ACTIVE_WORLD_OPTIONS : OPTIONS;

        const Page &page = pages_[iCurrentPage];
        const std::vector<PageItem> &page_items = page.items_;

        // Cache visible entries
        std::vector<int> visible_items;
        visible_items.reserve( page_items.size() );
        int curr_line_visible = 0;
        const auto is_visible = [&]( int i ) -> bool {
            const PageItem &it = page_items[i];
            switch( it.type )
            {
                case ItemType::GroupHeader:
                    return true;
                case ItemType::BlankLine:
                case ItemType::Option:
                    return groups_state[it.group];
                default:
                    cata_fatal( "invalid ItemType" );
            }
        };
        for( int i = 0; i < static_cast<int>( page_items.size() ); i++ ) {
            if( is_visible( i ) ) {
                if( i == iCurrentLine ) {
                    curr_line_visible = static_cast<int>( visible_items.size() );
                }
                visible_items.push_back( i );
            }
        }

        // Format name & value strings for given entry
        const auto fmt_name_value = [&]( const PageItem & it, bool is_selected )
        -> std::pair<string_col, string_col> {
            const char *IN_GROUP_PREFIX = ": ";
            switch( it.type )
            {
                case ItemType::BlankLine: {
                    std::string name = it.group.empty() ? "" : IN_GROUP_PREFIX;
                    return { string_col( name, c_white ), string_col() };
                }
                case ItemType::GroupHeader: {
                    bool expanded = groups_state[it.group];
                    std::string name = expanded ? "- " : "+ ";
                    name += find_group( it.group ).name_.translated();
                    return std::make_pair( string_col( name, c_white ), string_col() );
                }
                case options_manager::ItemType::Option: {
                    const cOpt &opt = cOPTIONS.find( it.data )->second;
                    const bool hasPrerequisite = opt.hasPrerequisite();
                    const bool hasPrerequisiteFulfilled = opt.checkPrerequisite();

                    std::string name_prefix = it.group.empty() ? "" : IN_GROUP_PREFIX;
                    string_col name( name_prefix + opt.getMenuText(), !hasPrerequisite ||
                                     hasPrerequisiteFulfilled ? c_white : c_light_gray );

                    nc_color cLineColor;
                    if( hasPrerequisite && !hasPrerequisiteFulfilled ) {
                        cLineColor = c_light_gray;
                    } else if( opt.getValue() == "false" || opt.getValue() == "disabled" || opt.getValue() == "off" ) {
                        cLineColor = c_light_red;
                    } else {
                        cLineColor = c_light_green;
                    }

                    string_col value( opt.getValueName(), is_selected ? hilite( cLineColor ) : cLineColor );

                    return std::make_pair( name, value );
                }
                default:
                    cata_fatal( "invalid ItemType" );
            }
        };

        // Draw separation lines
        wattron( w_options, BORDER_COLOR );
        for( const int &x : vert_lines ) {
            mvwvline( w_options, point( x, 0 ), LINE_XOXO, iContentHeight );
        }
        wattroff( w_options, BORDER_COLOR );

        if( recalc_startpos ) {
            // Update scroll position
            calcStartPos( iStartPos, curr_line_visible, iContentHeight,
                          static_cast<int>( visible_items.size() ) );
        }

        // where the column with the names starts
        const size_t name_col = 5;
        // where the column with the values starts
        const size_t value_col = 62;
        // 2 for the space between name and value column, 3 for the ">> "
        const size_t name_width = value_col - name_col - 2 - 3;
        const size_t value_width = FIXED_WINDOW_WIDTH - value_col - 4; // -4 для границ и отступов
        //Draw options
        for( int i = iStartPos;
             i < iStartPos + ( iContentHeight > static_cast<int>( visible_items.size() ) ?
                               static_cast<int>( visible_items.size() ) : iContentHeight ); i++ ) {

            int line_pos = i - iStartPos; // Current line position in window.

            mvwprintz( w_options, point( 1, line_pos ), c_white, "%d", visible_items[i] + 1 );

            bool is_selected = visible_items[i] == iCurrentLine;
            if( is_selected ) {
                mvwprintz( w_options, point( name_col, line_pos ), c_yellow, ">>" );
            }

            const PageItem &curr_item = page_items[visible_items[i]];
            auto name_value = fmt_name_value( curr_item, is_selected );

            const std::string name = utf8_truncate( name_value.first.s, name_width );
            mvwprintz( w_options, point( name_col + 3, line_pos ), name_value.first.col, name );

            trim_and_print( w_options, point( value_col, line_pos ), value_width,
                            name_value.second.col, name_value.second.s );

            opt_line_map.emplace( visible_items[i], inclusive_rectangle<point>( point( name_col, line_pos ),
                                  point( value_col + value_width - 1, line_pos ) ) );
        }

        scrollbar()
        .offset_x( 0 )
        .offset_y( iOffsetY + iTooltipHeight + 2 + iWorldOffset )
        .content_size( static_cast<int>( visible_items.size() ) )
        .viewport_pos( iStartPos )
        .viewport_size( iContentHeight )
        .apply( w_options_border );

        wnoutrefresh( w_options_border );

        //Draw Tabs
        int tab_x = 0;
        if( !world_options_only ) {
            mvwprintz( w_options_header, point( 7, 0 ), c_white, "" );
            for( int i = 0; i < static_cast<int>( pages_.size() ); i++ ) {
                wprintz( w_options_header, c_white, "[" );
                if( ingame && i == iWorldOptPage ) {
                    wprintz( w_options_header, iCurrentPage == i ? hilite( c_light_green ) : c_light_green,
                             _( "Current world" ) );
                } else {
                    wprintz( w_options_header, iCurrentPage == i ? hilite( c_light_green ) : c_light_green,
                             "%s", pages_[i].name_ );
                }
                wprintz( w_options_header, c_white, "]" );
                wputch( w_options_header, BORDER_COLOR, LINE_OXOX );
                tab_x++;
                int tab_w = utf8_width( pages_[i].name_.translated(), true );
                opt_tab_map.emplace( i, inclusive_rectangle<point>( point( 7 + tab_x, 0 ),
                                     point( 6 + tab_x + tab_w, 0 ) ) );
                tab_x += tab_w + 2;
            }
        }

        wnoutrefresh( w_options_header );

        const PageItem &curr_item = page_items[iCurrentLine];
        std::string tooltip = curr_item.fmt_tooltip( curr_item.group, cOPTIONS );
        fold_and_print( w_options_tooltip, point::zero, FIXED_WINDOW_WIDTH - 4, c_white, tooltip );

        if( ingame && iCurrentPage == iWorldOptPage ) {
            mvwprintz( w_options_tooltip, point( 3, 5 ), c_light_red, "%s", _( "Note: " ) );
            wprintz( w_options_tooltip, c_white, "%s",
                     _( "Some of these options may produce unexpected results if changed." ) );
        }
        wnoutrefresh( w_options_tooltip );

        wnoutrefresh( w_options );
    } );

    while( true ) {
        ui_manager::redraw();

        recalc_startpos = false;
        Page &page = pages_[iCurrentPage];
        auto &page_items = page.items_;

        options_manager::options_container &cOPTIONS = ( ingame || world_options_only ) &&
                iCurrentPage == iWorldOptPage ?
                ACTIVE_WORLD_OPTIONS : OPTIONS;

        std::string action = ctxt.handle_input();

        if( world_options_only && ( action == "NEXT_TAB" || action == "PREV_TAB" || action == "QUIT" ) ) {
            return action;
        }

        const PageItem &curr_item = page_items[iCurrentLine];

        const auto on_select_option = [&]() {
            cOpt &current_opt = cOPTIONS[curr_item.data];

#if defined(LOCALIZE)
            if( current_opt.getName() == "USE_LANG" ) {
                current_opt.setValue( select_language() );
                return;
            }
#endif

            bool hasPrerequisite = current_opt.hasPrerequisite();
            bool hasPrerequisiteFulfilled = current_opt.checkPrerequisite();

            if( hasPrerequisite && !hasPrerequisiteFulfilled ) {
                popup( _( "Prerequisite for this option not met!\n(%s)" ),
                       get_options().get_option( current_opt.getPrerequisite() ).getMenuText() );
                return;
            }

            if( action == "LEFT" ) {
                current_opt.setPrev();
            } else if( action == "RIGHT" ) {
                current_opt.setNext();
            } else if( action == "CONFIRM" ) {
                if( current_opt.getType() == "bool" || current_opt.getType() == "string_select" ||
                    current_opt.getType() == "string_input" || current_opt.getType() == "int_map" ) {
                    current_opt.setNext();
                } else {
                    const bool is_int = current_opt.getType() == "int";
                    if( is_int ) {
                        number_input_popup<int> popup( 0, current_opt.value_as<int>() );
                        popup.set_label( current_opt.getMenuText() );
                        int num = popup.query();
                        current_opt.setValue( num );
                    } else {
                        number_input_popup<float> popup( 0, current_opt.value_as<float>() );
                        popup.set_label( current_opt.getMenuText() );
                        float num = popup.query();
                        current_opt.setValue( num );
                    }
                }
            }
        };

        const auto is_selectable = [&]( int i ) -> bool {
            const PageItem &curr_item = page_items[i];
            switch( curr_item.type )
            {
                case ItemType::BlankLine:
                    return false;
                case ItemType::GroupHeader:
                    return true;
                case ItemType::Option:
                    return groups_state[curr_item.group];
                default:
                    cata_fatal( "invalid ItemType" );
            }
        };

        if( action == "MOUSE_MOVE" || action == "SELECT" ) {
            bool found_opt = false;
            sel_worldgen_tab = 1;
            std::optional<point> coord = ctxt.get_coordinates_text( w_options_border );
            if( world_options_only && with_tabs && coord.has_value() ) {
                // worldgen tabs
                found_opt = run_for_point_in<size_t, point>( worldgen_tab_map, *coord,
                [&sel_worldgen_tab]( const std::pair<size_t, inclusive_rectangle<point>> &p ) {
                    sel_worldgen_tab = p.first;
                } ) > 0;
                if( found_opt && action == "SELECT" && sel_worldgen_tab != 1 ) {
                    return sel_worldgen_tab == 0 ? "PREV_TAB" : "NEXT_TAB";
                }
            }
            coord = ctxt.get_coordinates_text( w_options_header );
            if( !found_opt && coord.has_value() ) {
                // option category tabs
                bool new_val = false;
                const int psize = pages_.size();
                found_opt = run_for_point_in<int, point>( opt_tab_map, *coord,
                [&iCurrentPage, &new_val, &psize]( const std::pair<int, inclusive_rectangle<point>> &p ) {
                    if( p.first != iCurrentPage ) {
                        new_val = true;
                        iCurrentPage = clamp<int>( p.first, 0, psize - 1 );
                    }
                } ) > 0;
                if( new_val ) {
                    iCurrentLine = 0;
                    iStartPos = 0;
                    recalc_startpos = true;
                    sfx::play_variant_sound( "menu_move", "default", 100 );
                }
            }
            coord = ctxt.get_coordinates_text( w_options );
            if( !found_opt && coord.has_value() ) {
                // option lines
                const int psize = page_items.size();
                found_opt = run_for_point_in<int, point>( opt_line_map, *coord,
                [&iCurrentLine, &psize]( const std::pair<int, inclusive_rectangle<point>> &p ) {
                    iCurrentLine = clamp<int>( p.first, 0, psize - 1 );
                } ) > 0;
                if( found_opt && action == "SELECT" ) {
                    action = "CONFIRM";
                }
            }
        }

        const int recmax = static_cast<int>( page_items.size() );
        const int scroll_rate = recmax > 20 ? 10 : 3;

        if( action == "DOWN" || action == "SCROLL_DOWN" ) {
            do {
                iCurrentLine++;
                if( iCurrentLine >= recmax ) {
                    iCurrentLine = 0;
                }
            } while( !is_selectable( iCurrentLine ) );
            recalc_startpos = true;
        } else if( action == "UP" || action == "SCROLL_UP" ) {
            do {
                iCurrentLine--;
                if( iCurrentLine < 0 ) {
                    iCurrentLine = page_items.size() - 1;
                }
            } while( !is_selectable( iCurrentLine ) );
            recalc_startpos = true;
        } else if( action == "PAGE_DOWN" ) {
            if( iCurrentLine == recmax - 1 ) {
                iCurrentLine = 0;
            } else if( iCurrentLine + scroll_rate >= recmax ) {
                iCurrentLine = recmax - 1;
            } else {
                iCurrentLine += +scroll_rate;
                while( iCurrentLine < recmax && !is_selectable( iCurrentLine ) ) {
                    iCurrentLine++;
                }
            }
            recalc_startpos = true;
        } else if( action == "PAGE_UP" ) {
            if( iCurrentLine == 0 ) {
                iCurrentLine = recmax - 1;
            } else if( iCurrentLine <= scroll_rate ) {
                iCurrentLine = 0;
            } else {
                iCurrentLine += -scroll_rate;
                while( iCurrentLine > 0 && !is_selectable( iCurrentLine ) ) {
                    iCurrentLine--;
                }
            }
            recalc_startpos = true;
        } else if( action == "NEXT_TAB" ) {
            iCurrentLine = 0;
            iStartPos = 0;
            recalc_startpos = true;
            iCurrentPage++;
            if( iCurrentPage >= static_cast<int>( pages_.size() ) ) {
                iCurrentPage = 0;
            }
            sfx::play_variant_sound( "menu_move", "default", 100 );
        } else if( action == "PREV_TAB" ) {
            iCurrentLine = 0;
            iStartPos = 0;
            recalc_startpos = true;
            iCurrentPage--;
            if( iCurrentPage < 0 ) {
                iCurrentPage = pages_.size() - 1;
            }
            sfx::play_variant_sound( "menu_move", "default", 100 );
        } else if( action == "RIGHT" || action == "LEFT" || action == "CONFIRM" ) {
            switch( curr_item.type ) {
                case ItemType::Option: {
                    on_select_option();
                    break;
                }
                case ItemType::GroupHeader: {
                    bool &state = groups_state[curr_item.data];
                    state = !state;
                    recalc_startpos = true;
                    break;
                }
                case ItemType::BlankLine: {
                    // Should never happen
                    break;
                }
                default:
                    cata_fatal( "invalid ItemType" );
            }
        } else if( action == "QUIT" ) {
            break;
        }
    }

    //Look for changes
    bool options_changed = false;
    bool world_options_changed = false;
    bool lang_changed = false;
    bool used_tiles_changed = false;
    bool pixel_minimap_changed = false;
    bool terminal_size_changed = false;

    for( auto &iter : OPTIONS_OLD ) {
        if( iter.second != OPTIONS[iter.first] ) {
            options_changed = true;

            if( iter.second.getPage() == "world_default" ) {
                world_options_changed = true;
            }

            if( iter.first == "PIXEL_MINIMAP_HEIGHT"
                || iter.first == "PIXEL_MINIMAP_RATIO"
                || iter.first == "PIXEL_MINIMAP_MODE"
                || iter.first == "PIXEL_MINIMAP_SCALE_TO_FIT" ) {
                pixel_minimap_changed = true;

            } else if( iter.first == "TILES" || iter.first == "USE_TILES" || iter.first == "DISTANT_TILES" ||
                       iter.first == "USE_DISTANT_TILES" || iter.first == "OVERMAP_TILES" ) {
                used_tiles_changed = true;

            } else if( iter.first == "USE_LANG" ) {
                lang_changed = true;

            } else if( iter.first == "TERMINAL_X" || iter.first == "TERMINAL_Y" ) {
                terminal_size_changed = true;
            }
        }
    }
    for( auto &iter : WOPTIONS_OLD ) {
        if( iter.second != ACTIVE_WORLD_OPTIONS[iter.first] ) {
            options_changed = true;
            world_options_changed = true;
        }
    }

    if( options_changed ) {
        if( query_yn( _( "Save changes?" ) ) ) {
            static_popup popup;
            popup.message( "%s", _( "Please wait…\nApplying option changes…" ) );
            ui_manager::redraw();
            refresh_display();

            save();
            if( ingame && world_options_changed ) {
                world_generator->active_world->WORLD_OPTIONS = ACTIVE_WORLD_OPTIONS;
                world_generator->active_world->save();
            }
            g->on_options_changed();
        } else {
            lang_changed = false;
            terminal_size_changed = false;
            used_tiles_changed = false;
            pixel_minimap_changed = false;
            OPTIONS = OPTIONS_OLD;
            if( ingame && world_options_changed ) {
                ACTIVE_WORLD_OPTIONS = WOPTIONS_OLD;
            }
        }
    }

    if( lang_changed ) {
        update_global_locale();
        set_language_from_options();
    }
    calendar::set_eternal_season( ::get_option<bool>( "ETERNAL_SEASON" ) );
    calendar::set_season_length( ::get_option<int>( "SEASON_LENGTH" ) );

    calendar::set_eternal_night( ::get_option<std::string>( "ETERNAL_TIME_OF_DAY" ) == "night" );
    calendar::set_eternal_day( ::get_option<std::string>( "ETERNAL_TIME_OF_DAY" ) == "day" );

#if !defined(EMSCRIPTEN) && !defined(__ANDROID__) && !defined(TUI)
    if( terminal_size_changed ) {
        int scaling_factor = get_scaling_factor();
        point TERM( ::get_option<int>( "TERMINAL_X" ), ::get_option<int>( "TERMINAL_Y" ) );
        TERM.x -= TERM.x % scaling_factor;
        TERM.y -= TERM.y % scaling_factor;
        const point set_term( std::max( EVEN_MINIMUM_TERM_WIDTH * scaling_factor, TERM.x ),
                              std::max( EVEN_MINIMUM_TERM_HEIGHT * scaling_factor, TERM.y ) );
        get_option( "TERMINAL_X" ).setValue( set_term.x );
        get_option( "TERMINAL_Y" ).setValue( set_term.y );
        save();

        resize_term( ::get_option<int>( "TERMINAL_X" ), ::get_option<int>( "TERMINAL_Y" ) );
    }
#else
    ( void ) terminal_size_changed;
#endif
    if( ingame ) {
        refresh_tiles( used_tiles_changed, pixel_minimap_changed, ingame );
    }
    return "";
}
